# TribesPad on Android and Raspberry Pi / Linux aarch64: findings and plan

Research date: 2026-10-22. Scope: read-only reading of this repository plus cited web sources; nothing was built. Labels: **Verified** = read in repo source or a cited page; **Estimate** = judgement from that evidence; **Unknown** = needs a test. Platform facts marked (knowledge) were not rechecked live.

## Short answer

A native Linux aarch64 build is a much smaller step than "porting to Android" sounds. The translated game code (about 2 GB of lifted LLVM IR) does not depend on the target. The memory model already handles host pages larger than 4 KiB, because Apple ARM64 uses 16 KiB pages. The guest window does not need low addresses. The Apple-specific code sits in a small number of files. The riskiest unknowns are two: whether the lift reproduces byte-identical bases on a non-Mac host, and whether the hardware-permission checked memory (memfd alias, SIGSEGV decoding, VMA counts) behaves on Linux/Android kernels. Both can be tested cheaply before any APK work. Recommended order: tell Android players about GameNative/Winlator now; run G0/G1 (memory model plus Linux lift determinism); then a headless linux-aarch64 engine; then Raspberry Pi 5; only then Android packaging.

## 1. Inventory: Apple-specific versus portable

### Sizes (Verified, line counts from wc)

| Area | Files | Lines (C/ObjC/C++ excluding .inc tables and vendor) | Apple-specific? |
|---|---|---|---|
| port/core (Win32 services, guest CPU helpers, memory, threads, sockets, audio/GL bridges) | ~170 C/H plus .inc tables | ~16,000 | About 18 files contain Darwin/BSD calls, in small sections |
| port/llasm-support (x87/SSE/MMX helpers) | 17 | ~4,100 | 1 incidental match (a struct field named _dispatch_target); portable |
| port/compiler (GuestMemoryPass, ExternalMemoryPass LLVM plugins) | 2 | ~1,050 | ExternalMemoryPass allowlists Darwin libc/Mach ABIs by name |
| port/apple (app shell, UIKit controls, display, font, GL context) | 21 | ~2,360 | Mostly Apple; gl4es_context.c, sdl_main.c and the C half of mobile_controls are largely portable |
| tests/qol_native_bootstrap.c (the real engine main, 1,119 lines) | 1 | 1,119 | Portable except pthread_threadid_np |
| Translated game (Tribes2.exe, IFC22.dll, Mss32.dll, 11 Miles codecs) | 166 recreated files, ~2 GB | n/a | Target-neutral IR (see below) |
| Build scripts | 842 .py (38.5k lines) | | 396 mention xcrun / apple triples / /opt/homebrew, but only 48 are not test-*.py |

### Translated code (Verified)

engine-routed.ll and merged-dispatch.ll begin with target triple = "arm64-apple-macos12" and target datalayout = "e-p:64:64-f64:64:64-n32:64". That layout has no mangling field and no Apple-only fields. scripts/checked_support.py removes any m: field and inserts m:o only when '-apple-' is in the target. Codegen runs clang -target <t> -Wno-override-module, so the module triple is overridden at compile time. For aarch64-linux-gnu or aarch64-linux-android29, the same patched .ll bytes can be compiled with a different -mtriple and ELF mangling (m:e), so the SHA-256 checks on the recreated files still hold. **Estimate:** IR-level carry-over is effectively 100%. **Residual ABI risks (Estimate):** Darwin AAPCS64 differs from standard AAPCS64 in variadic argument passing (Darwin puts variadic args on the stack) and in caller-side extension of i8/i16 arguments. Because translated code and native helpers would both be compiled for the same Linux target, this only matters where hand-written code or the ExternalMemoryPass type checks assume the Darwin form. x18 is reserved on both platforms (Android uses it for the shadow call stack).

### Guest address space (Verified)

- tests/qol_native_bootstrap.c:1018 reserves the window as mmap(NULL, 4 GiB, PROT_NONE, MAP_PRIVATE|MAP_ANON). It lets the kernel choose the address, and every guest access is base + 32-bit address via cpu->_pointer_offset. **No low-4 GB or MAP_FIXED placement is needed**, so Android mmap_min_addr, ASLR and MAP_FIXED_NOREPLACE in the low 4 GB don't matter.
- Guest-visible range: TRIBES_GUEST_MINIMUM_ADDRESS 0x00010000 to TRIBES_GUEST_MAXIMUM_ADDRESS 0x7ffeffff (port/core/guest_system.h). Windows permissions are tracked per 4 KiB page.
- Hardware permissions (port/core/guest_memory_checks.c, TRIBES_HARDWARE_PERMISSIONS): sysconf(_SC_PAGESIZE) may be 4 KiB to 64 KiB. A host page whose 4 KiB guest permissions agree gets that protection directly. A mixed page, or a writable page holding published code, is "narrowed" to PROT_NONE/PROT_READ, and its faults are decoded and emulated through a private read-write alias. This already runs with 16 KiB pages on iOS. **Implication:** 16 KB Android devices take the same code path as iPhone/iPad. 4 KB devices have no mixed pages at all, only published-code narrowing, so they should fault less.
- The Darwin-only part is hardware_configure() and the handler:
  - vm_allocate(VM_FLAGS_FIXED) creates a 64 KiB guard after the window. Linux equivalent: mmap(MAP_FIXED_NOREPLACE) or reserve window plus guard in one mapping.
  - vm_remap(copy=FALSE) creates an alias of the first 2 GiB. **Linux can't alias MAP_PRIVATE anonymous memory**, so the window must be created from a memfd (memfd_create or Android ASharedMemory_create), mapped MAP_SHARED twice. That changes the reservation in qol_native_bootstrap.c, and the mapping becomes shared memory for accounting purposes.
  - vm_region_64 queries a region. Linux: track it in our own tables, or read /proc/self/maps.
  - Registers come from __darwin_mcontext64 (__ss.__x, sp, fp, lr, pc) and the write bit from __es.__esr. Linux: ucontext_t.uc_mcontext.regs[31]/sp/pc, with ESR read from the esr_context record (ESR_MAGIC) in uc_mcontext.__reserved (knowledge, unverified here).
- Decommit/release call madvise(MADV_DONTNEED). On a shared memfd that does not discard pages. Correctness is preserved because commit paths memset to zero explicitly (guest_heap.c, guest_virtual_memory.c, Verified), but memory is only returned with fallocate(FALLOC_FL_PUNCH_HOLE) or MADV_REMOVE.
- **New Linux risk (Estimate):** each run of distinct protection is a VMA. Linux caps VMAs at vm.max_map_count (default 65530). Darwin has no comparable small cap. Windows-style 4 KiB permission patterns on 4 KB kernels could approach it. Measure in G0.
- Extended Virtual Addressing is an iOS entitlement only. Android/Linux arm64 user space is 39-bit (512 GiB) or larger on common kernels (knowledge), ample for a 4 GiB window plus a 2 GiB alias.

### Darwin/BSD calls in port/core (Verified by grep; Linux replacement is Estimate)

| File | Darwin use | Linux / Android replacement |
|---|---|---|
| guest_memory_checks.c | mach vm_allocate / vm_remap / vm_region_64, __darwin mcontext | memfd double map, mmap guard, Linux mcontext + esr_context |
| guest_system_query.c | mach host_processor_info | /proc/stat |
| guest_registry.c | sysctlbyname | /proc, sysconf, or fixed values |
| guest_file.c, guest_find.c | fsetattrlist, st_birthtime, st_flags, st_mtimespec, statfs f_fstypename, O_SYMLINK | utimensat, statx (btime), st_mtim, statfs f_type, O_NOFOLLOW/O_PATH |
| guest_condition.c, guest_threads.c, guest_user_messages.c, native_handles.c | pthread_cond_timedwait_relative_np | pthread_cond_timedwait with CLOCK_MONOTONIC condattr |
| native_winsock.c | sin_len, SO_NOSIGPIPE, pthread_threadid_np | drop sin_len, MSG_NOSIGNAL, gettid |
| guest_adapters.c | getifaddrs with AF_LINK / sockaddr_dl | getifaddrs with AF_PACKET (bionic has getifaddrs from API 24, knowledge) |
| guest_bcrypt.c, guest_file.c, server_query.c | arc4random(_buf) | bionic has arc4random_buf; glibc 2.36+ too, else getrandom |
| guest_sdl_backend.c, guest_graphics.c | TargetConditionals, UIKit/EAGL drawable assumptions (FBO not 0, no swap-interval setter) | Android/Linux EGL window surface: framebuffer 0, swap interval available |
| guest_heap.c, guest_virtual_memory.c | MADV_DONTNEED semantics | see above |
| tests/qol_native_bootstrap.c | pthread_threadid_np, MAP_PRIVATE window | gettid, memfd MAP_SHARED window |

Threads and TLS are pthreads plus C11 _Thread_local. The x86 FS/TEB is a guest-memory structure whose per-thread context is _Thread_local (guest_teb.c), so it is portable. Bionic ELF TLS needs Android 10 / API 29 (knowledge), which suggests minSdk 29. Win32 file paths are mapped by the runtime, and the app's root comes from mobile_game_paths.m (Apple) and would come from SDL_GetPrefPath / the Android internal storage path.

### ExternalMemoryPass (Verified)

ExternalMemoryPass.cpp approves native calls from checked code by exact name and type. Darwin-specific entries include realpath$DARWIN_EXTSN, pthread_main_np, pthread_cond_timedwait_relative_np, clock_getres with constant 6 (CLOCK_MONOTONIC on Darwin; it is 1 on Linux), sysctlbyname, the mach calls, and CoreText CTFontGetBoundingRectsForGlyphs (the font path, so font.m uses CoreText; Linux needs FreeType or stb_truetype). A Linux build needs a Linux allowlist. That is a security-relevant review because it changes source-bound receipts, and the HANDOFF says a change to guest_memory_checks.c or ExternalMemoryPass.cpp invalidates eleven receipts.

### Graphics, audio, network, input (Verified unless marked)

- **Graphics:** GL 1.x → GL4ES → ANGLE (Metal). angle_sdl.c is EGL on ANGLE plus SDL_Metal view creation. gl4es_context.c streams through fenced rings (glFenceSync, glClientWaitSync, glMapBufferRange, GL_PIXEL_UNPACK_BUFFER), which needs GLES 3.0. Fan/loop expansion exists only because Metal lacks them; native GLES has them, and TRIBES_GL_STREAMING=0 already disables streaming. On Android or the Pi, GL4ES can drive the native GLES 3.x driver through SDL's EGL context. ANGLE-on-Vulkan stays a fallback for bad drivers (Estimate).
- **Audio:** pinned OpenAL Soft 1.24.3 (scripts/build-native-openal.py) builds with the OPENSL and OBOE backends explicitly OFF and links CoreAudio/AudioUnit. Android: turn on OpenSL (in the NDK) or Oboe. Pi: ALSA/PipeWire/Pulse. The Miles → OpenAL bridge (guest_openal.c) is portable.
- **Network:** curl plus LibreSSL built from pinned tarballs (build-http-backend.py) is portable. **Unknown:** how CA roots are supplied on Apple. Android has no CA bundle file in the usual Linux place, so ship one or read /system/etc/security/cacerts. Winsock is BSD sockets (fixes listed above).
- **Input/windowing:** the QoL client is SDL3-based, and TribesPad bridges guest SDL calls to native SDL3 (guest_sdl_backend.c, 2,848 lines, mostly portable). SDL3 has Android (SDLActivity Java glue, touch, game controllers) and Linux (X11/Wayland/KMSDRM) backends. The UIKit touch overlay (mobile_controls.m, 971 lines) must be redone. Its SDL-event logic in mobile_controls.c can be reused, and drawing the overlay in GL inside the game surface would serve both Android and Pi touchscreens (Estimate).

### Carry-over estimate (Estimate)

- Translated game and its patches: ~100%.
- port/llasm-support: ~100%.
- port/core: ~95% of lines unchanged, with Linux branches in ~18 files. The memory-check and fault-handler port is the only delicate part.
- port/compiler: GuestMemoryPass unchanged; ExternalMemoryPass needs a Linux allowlist.
- port/apple: ~25–35% reusable (gl4es_context.c, sdl_main.c, EGL half of angle_sdl.c, mobile_controls.c). UIKit scenes, content_area, display, the CoreText font and app packaging are rewritten.
- Build: about 48 non-test scripts need a target table (triple, sysroot, NDK clang) in place of xcrun and /opt/homebrew paths.
- Overall, about 85–90% of the hand-written runtime carries over by lines. The effort concentrates in memory/faults, the app shell, touch UI and build plumbing.

## 2. Android specifics

- **Triple:** aarch64-linux-android29 (or higher) with NDK clang. The repo pins LLVM 20.1.8; the NDK's clang is a different build, but upstream LLVM 20 can target Android using the NDK sysroot (Estimate). Same IR; data layout gains m:e.
- **16 KB pages:** already supported by the checker. Link with -Wl,-z,max-page-size=16384 so ELF segments load on 16 KB kernels. Google Play: from 1 February 2027, Play blocks updates for apps targeting Android 15 (API 35)+ that don't support 16 KB pages on 64-bit devices; the original November 2025 date was moved. NDK r28+ aligns ELF to 16 KB by default ([Android Developers](https://developer.android.com/guide/practices/page-sizes), [Android Developers Blog](https://android-developers.googleblog.com/2025/05/prepare-play-apps-for-devices-with-16kb-page-size.html)). This matters only for Play, but a sideloaded build should be 16 KB-aligned anyway.
- **Low-4 GB mmap:** not required (above). The 4 GiB PROT_NONE reservation plus a 2 GiB alias should fit the address space. **Unknown:** Android low-memory-killer behavior with a large shared memfd, plus VMA counts. Test in G0.
- **W^X:** no JIT, so no issue. Published code is native ARM in the APK, and runtime-written x86 is never executed (Verified, README/HANDOFF).
- **GL:** GL4ES on native GLES 3 is the natural path. GL4ES grew up on Pandora/ARM Linux and is used on Android by Box86/Box64-era launchers (knowledge). Mali/PowerVR driver quality varies, and GameNative's FAQ also names Snapdragon/Adreno as the best-supported family ([FAQ](https://gamenative.org/faqs/)).
- **SDL3 Android:** the SDLActivity Java glue drives the C main. Touch, gamepads and the soft keyboard come through SDL events, and the existing event bridge expects those.
- **OpenAL Soft:** enable OpenSL or Oboe (AAudio).
- **APK packaging:** gradle or a minimal aapt2/apksigner flow with a self-signed key. The IPA carries ~1 GB of game data. In an APK, store it uncompressed and either read it in place or extract it to internal storage on first run. **Unknown:** whether the guest file layer can read from the APK, so extraction is the safe default.
- **Where the build runs:** today the lift needs SRW (C, built with SCons), llasm (D, built with LDC), LLVM 20 and Python, all portable in principle. M-HT's SR project, the upstream of SRW/llasm, already targets Linux arm/arm64 (ref/sr/README.md, Verified). But build-tribespad.sh hard-fails without an Apple Silicon Mac and Xcode. The HANDOFF warns "Other Xcode versions may lift differently; the build stops with a clear message if a base digest differs." So **whether a Linux host reproduces the bases byte for byte is Unknown and decisive**. If it does, a Linux x86_64/arm64 host or an ubuntu-24.04-arm GitHub runner can build the APK. Windows works via WSL2. On-device building is impractical on phones (peak 6.3 GB RAM in one process, ~10–15 GB disk, Verified from REPRODUCIBLE-BUILD/README) but plausible on a 16 GB Pi 5 (Estimate).
- **Store policy:** a Play listing would distribute translated game code and data, which the README explicitly does not do. Use the PadMint model of personal builds, sideloaded, with GitHub releases carrying source only. Apply the same warning the README gives for public-fork IPA artifacts. Play policy specifics were not checked live.

## 3. GameNative / Winlator comparison

- **What it is (Verified):** GameNative runs Windows PC games locally on Android through Wine/Proton plus an x86 translator (FEXCore or Box64), with Steam/Epic/GOG integration ([gamenative.app](https://gamenative.app/), [FAQ](https://gamenative.org/faqs/)). Winlator is the older standalone app built on the same Wine + Box64 idea ([winlator.dev setup guide](https://winlator.dev/setup-winlator/)).
- **Requirements (Verified, [GameNative FAQ](https://gamenative.org/faqs/)):** Snapdragon with an Adreno GPU gives the best compatibility, and Snapdragon 8 Gen 2 or newer is recommended for demanding 3D games. Mali "may work" but has fewer driver options. RAM: 4 GB only for light 2D or older games, 8 GB as the baseline, 12 GB or more for modern 3D. Use the latest Turnip driver on Adreno, or switch versions or fall back to the system driver on crashes or black screens.
- **CPU translator (Verified, same FAQ):** start with FEXCore for most modern 64-bit games. "Older or 32-bit games may require Box64." Tribes 2 is a 2001 32-bit x86 game, so Box64 is the likely starting point (Estimate). The FAQ says there is no universal best choice and that the emulator, Proton, driver and game settings may need changing together.
- **Tribes 2 specifically:** none of the results I gathered contained a GameNative or Winlator compatibility report, benchmark or configuration for Tribes 2. The community claim that it runs is **unverified here**, and frame rate on Android is **Unknown**. As a 2001 OpenGL title it should be light by the FAQ's own categories, but that is an estimate.
- **TribesNEXT login and QoL under Wine/Box64 on Android: Unknown.** My search on TribesNEXT CPU and GL requirements did not run, so I make no claim about them. Local evidence (Verified, docs/HANDOFF.md): on this Mac the QoL client did not start under Wine (an SDL3 video init failure under Whisky, an IFC22 fault under CrossOver), while plain Tribes2.exe ran under CrossOver. Android Wine may hit the same QoL problem.
- **Trade-off (Estimate):** GameNative costs TribesPad nothing today. But it is a Wine + JIT translation stack with per-device tuning, it has no Tribes touch layout (keyboard/mouse or a controller is likely needed), and QoL is unproven there. TribesPad native would bring AOT code, checked memory, the existing touch/controller design, the QoL client and the reference-trace fidelity claim.

## 4. Raspberry Pi 5 / Linux aarch64

It is the same runtime work minus Android packaging. SDL3 runs on X11/Wayland/KMSDRM, OpenAL Soft on ALSA/PipeWire, and GL4ES on Mesa.

- **Page size (Verified, [Raspberry Pi kernel docs](https://www.raspberrypi.com/documentation/computers/linux_kernel.html)):** the Pi 5's default kernel_2712.img uses 16 KiB pages; kernel8.img runs with 4 KiB pages. The default Pi 5 therefore exercises the same narrowing path as iOS and 16 KB Android. That makes it a good stand-in for the hard case, and switching kernels gives a 4 KiB comparison on the same hardware.
- **GPU (Verified, [Pi 5 product brief](https://datasheets.raspberrypi.com/rpi5/raspberry-pi-5-product-brief.pdf)):** VideoCore VII supports OpenGL ES 3.1 through Mesa's v3d driver. That is enough for gl4es_context.c's GLES 3 streaming rings (fences, mapped buffers, pixel-unpack buffers).
- **Performance: Unknown.** A Cortex-A76 is much slower per core than the M4 Mac behind the ~30 FPS Simulator figure, and even physical-iPad frame rate isn't measured yet. A 16 GB Pi could plausibly run the whole build on-device if G1 passes (Estimate; the build peaked at 6.3 GB RAM in one process).

## 5. Gated plan

| Gate | Goal | Pass criterion | Cost (Estimate) |
|---|---|---|---|
| G0 | Prove the checked-memory model on Linux/Android kernels without the game | Standalone C test: 4 GiB memfd MAP_SHARED window, 2 GiB alias, guard, SIGSEGV handler with Linux mcontext/esr_context, the narrowing/emulation path of guest_memory_checks.c, a fault census, and a VMA count under worst-case permission patterns. Run as an adb shell binary on one 4 KB and one 16 KB Android device (Pixel 8+ 16 KB developer option, knowledge) plus a Pi 5. Pass: all cases pass, emulated faults resume, VMA count is well below max_map_count, no LMK kill | 2–4 days |
| G1 | Lift determinism off the Mac | build-tools.py plus translation-patches.py bases on Linux arm64 (and x86_64) produce SHA-256-identical bases to config/translation-patches.json; patches apply. If not, find the cause (compiler, locale, hash order) or record per-host bases | 2–5 days, in parallel with G0 |
| G2 | Headless linux-aarch64 engine | Compile the patched IR plus port/core for aarch64-linux-gnu with a Linux ExternalMemoryPass allowlist. The game reaches engine entry; ideally a dedicated or offline headless scenario runs tests/sim_reference_trace.cs bit-identical to the Mac trace | 2–3 weeks |
| G3 | Windowed Linux/Pi 5 | SDL3 plus GL4ES on Mesa GLES, OpenAL; offline Katabatic with bots, FPS recorded, clean Quit | 1–2 weeks |
| G4 | Android APK | NDK build, SDLActivity, 16 KB-aligned ELF, OpenAL OpenSL/Oboe, data extraction, offline match on one Adreno device, FPS and thermals recorded | 2–3 weeks |
| G5 | Android play acceptance | Touch overlay (shared C/GL), controller, TribesNEXT login and joining, background/resume, a long session; Mali device check | 2+ weeks |
| G6 | Distribution | PadMint linux host target producing APK/tarball; player-check equivalent on clean Linux; source-only releases | 1 week |

Stop rules: if G0 fails because the memfd alias or the VMA count is unworkable, fall back to the bracketed software-checked mode (6–7 FPS in the Simulator, so not viable for play) or redesign the memory model. In that case point Android players to GameNative for good. If G1 fails and can't be made deterministic, the build stays Mac-hosted with a cross-compiled APK output. That still works, but it keeps the Mac requirement.

## Ranked recommendation

1. Now, at no cost: point Android players to GameNative (Adreno, 8 GB+, Box64 first for a 32-bit game). Say plainly that no confirmed Tribes 2 report was found and that TribesNEXT login and QoL under Wine are unproven.
2. Run G0 and G1 together. They are cheap, need no APK, and settle the two assumptions everything else depends on.
3. Make Raspberry Pi 5 / Linux aarch64 the first native target (G2 → G3). It is the same runtime work without Android packaging, its default kernel uses 16 KiB pages, and kernel8.img gives a 4 KiB comparison.
4. Build the Android APK (G4–G6) only after G3 shows playable frame rates, distributed as personal builds and never on Play.

