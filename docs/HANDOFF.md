# TribesPad handoff — 21 October 2026

## Latest: physical devices and online play (18–21 October)

TribesPad now runs on a physical 12.9-inch iPad Pro (6th generation) and iPhone 14 on
iOS 27, signs in to TribesNEXT and joins online Classic CTF servers, played with touch
and an Xbox controller. The owner authorized online sign-in and joining; the older
"offline only" scope below is superseded. Changes on branch `codex/menu-icon-ios27`:

- **Device runtime:** iOS 27 scene lifecycle; runs without Extended Virtual Addressing
  (checked-memory alias limited to the 2 GiB user space); speaker audio session; no
  microphone; session log in `Library/Logs/TribesPad.log` (previous session kept).
- **TribesNEXT login:** the 500 fps software limiter (`setFramerateLimit`, an SDL timer
  import stop) is disabled; account download saves via SetFilePointerEx, GetFileSizeEx,
  FlushFileBuffers, SetEndOfFile and SystemFunction036; Remember Password persists the
  game's registry keys in `tribes2-registry.bin`.
- **Online crashes fixed from logs:** GL extensions with no providers are hidden from the
  game; every OpenGL 1.1/WGL function the game loads is implemented or a logged no-op
  stub (`config/runtime/gl-unprovided.json`); `glColor3f` implemented; curl POST options
  for hosting; LCMapStringEx route; PF_FASTFAIL_AVAILABLE.
- **Display and input:** UI scale fills iPad (4x on 12.9-inch); glyph sheets sampled
  nearest (no boxes around text); touch/trackpad/controller layouts and a first-run
  controller card; keyboard opens on the login fields.
- **Still open:** frame rate and heat on device under load; long online sessions;
  clipboard paste, IP-dialog host names and demo deletion are import stops.

## Read this first

**The original game runs offline on Mac and in the iPad Simulator, now near 30 FPS in the Simulator with the MMU-enforced build. It is not yet a finished, accepted Apple port.** The primary engineering problem is runtime performance, followed by physical-device validation and reproducible packaging. This is not currently blocked on another missing startup API or on permission to use this Mac.

On 12 October a fresh clone became a one-command build: `scripts/build-tribespad.sh` downloads the official game files and pinned sources, lifts the game on the player's Mac, recreates the tested translated code from small patches and builds an iPhone/iPad IPA, a Mac app or a Simulator app ([Reproducible build](REPRODUCIBLE-BUILD.md)). PadMint's recipe (`padmint.json`) and a GitHub runner workflow use it. A clean-clone Simulator build played offline Katabatic from a fresh install, after two first-run file services were added (`GetFileInformationByHandle`, `MoveFileExW`, plus specific CreateFile rights); before that every fresh install aborted on its first TribesNEXT download. The Mac app now uses the same engine build and GL4ES → ANGLE (Metal) renderer as iOS and played offline Katabatic.

On 10 October the MMU-enforced build took offline Katabatic from 6-7 to a steady ~30 FPS in the Simulator, every touch control was played by touch, an end-of-match map cycle and clean quit were verified, and the touch controls, TribesPad menu, app icon and public README were added. All of it is merged to main. No full-engine app or build process was left running.

Start here instead of replaying the enormous checkpoint histories in [STATUS](archive/STATUS.md) and [EXECUTION-PLAN](archive/EXECUTION-PLAN.md). Their old “next,” “blocked,” and live-process statements are historical. The heartbeat's repeated GetQueuedCompletionStatusEx boundary was superseded long ago.

**Historical scope (superseded 19 October by the owner):** offline bot matches and server discovery only. The [PRD](Tribes2-Online-HD-Target.md) describes the broader online/HD product target; it does not override the user's later restriction. Online acceptance remains deferred and unverified, rather than silently satisfied by discovery.

## What actually works, and what does not

| Area | Observed result | Remaining acceptance |
| --- | --- | --- |
| macOS original engine | Same checked engine and GL4ES/ANGLE renderer as iOS (`build-ipad-engine.py --platform macos --angle`); fresh profile reached offline Katabatic with bots and spawned. Fullscreen now fills a Retina display: the game asks for the desktop size without high-density modes, and SDL had answered with the scaled 2048x1332 mode, which left a window running off a 1470x956 screen; the closest-mode lookup now includes Retina modes on the Mac. Mouse-driven menus, a match and Cmd-Q with saved preferences verified | Performance measurement, longer play; a fresh profile shows the Dedicated host option ticked |
| iPad Simulator original engine | Metal rendering; offline Katabatic at ~30 FPS (MMU-enforced build); every touch control (move, look, fire, jet, ski/jump, weapon, grenade, pack, inventory) played by touch; map cycle and clean quit; 30-minute soak; saved profile | Simultaneous gestures, full visual/audio fidelity |
| iPad lifecycle/inventory | Two offline map loads in one 21+ minute session; equipment selection and station changes; original Quit exits code 0 | Station setup used diagnostic console positioning; a fully touch-navigated route remains untested |
| Physical iPad | Unsigned IPA from a clean clone passes PadMint's package check and carries Extended Virtual Addressing | Signing, installation, launch, RAM/thermal/performance and input testing; no physical gameplay evidence |
| iPhone | iPhone 17 Simulator: fresh install to an offline Katabatic match, spawn and jet by touch. Phones report fewer than 640x480 points in landscape, so those displays are described to the guest in pixels (`guest_sdl_backend.c` `pixel_modes`); without that, video setup failed and Miles fell back to an untranslated DirectSound mixer path. The game area is inset by the side safe area on both sides (`port/apple/content_area.m`): the guest sees a 2250x1206 display, draws into its own Metal layer, and pointer positions are offset to match, so the HUD stays clear of the Dynamic Island in either landscape orientation; iPad and Mac have no side insets and are unchanged. Offline profiles save `$pref::*` once a minute (`autoexec.cs`), because iOS ends apps without the game's quit-time save; without it the license and new warriors returned on every launch QoL scales the UI by the display scale and rounds up, so the 3x phone got 750x402 and the 640x480 menus were cropped; the reported display scale is capped at the largest whole scale that keeps 640x480 visible (2 on iPhone 17; iPad keeps 2). Training, Warrior Setup, Host, the server browser (26 servers found with the internet-browser test profile) and the in-game lobby fit; background and resume kept the match running; Leave Game and Launch → Quit end cleanly | Device play |
| Controller | Extended-gamepad mappings and native component checks | Physical controller gameplay on an actual device |
| Server discovery | Mac original browser displayed 26 community servers and refreshed information; the iPhone 17 Simulator's original browser listed 26 servers with the same isolated test profile ([screenshot](validation/iphone-server-discovery-2026-10-12.jpg)) | Device discovery; discovery does not prove joining/authentication |
| Online matches | No accepted authenticated session | Deferred under current user scope |
| HD/audio | 235 distinct captured upload-input buffers match selected HD pixels; fog/x87 and WAV/MP3 defects repaired | GPU sampling/scene fidelity, audible quality and speaker output; data correctness is not sensory acceptance |

Key evidence, each with its own limitations:

- [Mac offline controls and server browser](validation/macos-retina-input.json), [Mac screenshot](validation/macos-retina-katabatic-2026-10-08.png), [HD upload inputs](validation/macos-hd-upload-inputs.json).
- [Latest iPad component/build/gameplay results](validation/ipad-mmx-vectors.json), [gameplay screenshot](validation/ipad-mmx-vectors-gameplay-2026-10-09.png).
- [Longer reload/inventory session](validation/ipad-offline-reload-inventory.json), [memory-group comparison](validation/ipad-memory-groups.json).

## Performance: the central unresolved problem

**Current result (10 October, MMU-enforced build):** offline Katabatic, two bots, observer view in the iPad Air 11-inch (M4) Simulator runs at **27.5–29.3 FPS** with worst frames of 57–221 ms, against 6.0–7.0 FPS for the bracketed build on the same route. The map now loads in about 70–80 seconds instead of 221. Evidence: [hardware permissions run log](validation/ipad-hardware-permissions.json); the earlier baseline and rejected experiment are in [frame timing and guard review](validation/ipad-frame-timing-guard-review.json). Measured with the opt-in `TRIBES_FRAME_TIMING=1` swap logger (CPU-side submission, not GPU completion). Run-to-run variation on this shared Mac is about 8%.

**What changed, in order of effect:**

1. **MMU-enforced checked memory** (`--hardware-permissions`): guest loads and stores run natively. Every 16 KiB host page whose four 4 KiB guest permissions agree gets exactly that protection; a mixed page, or a writable page holding published code, is narrowed and its faults are decoded, checked against the exact table and published intervals, then aborted or emulated through a private alias and resumed. Faults on pages the checker did not narrow still stop. A census found 0.47 mixed-page accesses per million in gameplay. 6.75 → 19.8 FPS; binary 104 → 64 MB; build 13 → 7 minutes.
2. **Lighter GL entry and O(1) indirect-target check:** pure GL calls keep binding/owner checks but no longer copy and compare every SDL public buffer per call (still revalidated at the next SDL entry before SDL uses them); indirect calls use a frozen bitmap of the same 523,444 targets instead of a binary search.
3. **ANGLE stall removal:** Metal has no triangle fans or line loops, so ANGLE generated their indices per draw from a capped pool and waited for the GPU when it ran out (~6.5 ms per frame). The GL4ES context now streams client arrays into its own fenced ring buffers and expands fans/loops into indexed triangles/lines (`TRIBES_GL_STREAMING=0` disables it).
4. **Exact x87 loads without SoftFloat's thread-local environment**, verified bit- and flag-identical for all 2^32 binary32 inputs and 200 M binary64 cases.
5. **Texture uploads through a fenced pixel-unpack buffer ring:** `glTexSubImage2D` previously copied synchronously into Metal (23% of samples in the Simulator under `MTLSimTexture replaceRegion`). It now stages through a ring of `GL_PIXEL_UNPACK_BUFFER`s and falls back for unusual unpack state. Scripted-player median 24.9 → 29.5 FPS; textures visually correct.

6. **Exact hardware path for scalar SSE add/sub/mul/div:** finite operands and results well inside the normal range, round-to-nearest, no host flush-to-zero; the precision flag comes from an exact TwoSum/FMA error term. Everything else stays on SoftFloat. 600 M differential cases (macOS and Simulator) match the SoftFloat reference bit-for-bit, and `test-sse-arithmetic` passes its Rosetta x86 corpus ([test](../tests/sse_fast_path_test.c)). The host mode is probed with arithmetic because the external-memory pass correctly rejects inline assembly.

**Touch play (Simulator):** the controls were redrawn in the HaloPad/SunPad style (analog move stick, Fire with a ring of Jet, Ski / Jump, Grenade and Weapon, Pack and Inventory above the stick, an invisible look surface, round Keyboard / controls / Menu buttons). Spawn, move, look, Jet (height 95.6 to 121.7 with energy 60 to 18 during a 2.5 s hold), Ski / Jump, Weapon (Blaster to Spinfusor), Grenade (count 5 to 4) and Inventory open/Done were driven by real Simulator touches. A held control had been released on every relayout, so Jet only pulsed; layout now releases controls only when the visible area changes; touch response starts within about 0.1-0.15 s. On a quieter Mac (load 3.8) the touch-played match holds 29.9-31.3 FPS with worst frames 52-90 ms over three minutes; the steady 30.0 suggests a half-refresh present cadence worth investigating next. Turning does not reduce frame rate (14.4-15.8 FPS while dragging vs 15.2-15.7 idle on a Mac loaded by remote-desktop streaming).

**Menu and settings:** the ··· button opens a TribesPad menu modeled on HaloPad: Game Menu (sends Escape), show/hide controls, the original console, touch-control size/opacity/look sensitivity, a left-handed mirrored layout, hiding with a controller, an FPS counter (presented frames per second, counted by the renderer) and Help. Size, FPS counter and left-handed layout were checked in the Simulator. Next performance lead: with `$pref::Video::disableVerticalSync = 1` the same match runs 36-37 FPS instead of a steady 30, so the default VSync-on Apple profile caps play at half refresh in the Simulator; check this on a physical iPad before changing the default.

7. **Exact hardware path for x87 arithmetic** ([x87_exact.h](../port/core/x87_exact.h)). The game runs the x87 at full 64-bit precision (control word 0x37f, read from the running game), so ordinary double math is not equivalent in general. When both operands are exactly representable as doubles and an exact error term (TwoSum or an FMA residual) proves the true result is too, every correct x87 returns that exact value with no flags, so hardware computes it. Everything else still uses Bochs SoftFloat. A differential test against the game's own tb_bo_arithmetic ([test](../tests/x87_exact_test.c)) passes 400 M cases (27 M on the exact path) on macOS and 40 M in the Simulator; it caught and prevented an underflow bug during development. The SSE fast path now also accepts zero operands. In play, calls into Bochs arithmetic fell from 586 to 64 samples per 10 s and floating point from 13.5% to 7.0% of the game thread; the reference trace is unchanged. Frame rate under the Simulator is now mostly GPU-fence bound, so the CPU saving should show more on hardware.

**Player gameplay (scripted):** a TorqueScript playtest inlined into the app container's `base/autoexec.cs` hosts Katabatic, spawns, then moves, turns, fires, jets and jumps. The player view renders correctly at a median of 29.5 FPS (windows 19–43 FPS depending on scene). The run7 binary ran over 30 minutes at about 29.6 FPS with a 391 MB footprint and no crash. The hardware build also compiles for physical iPad but has not been installed (no device or signing identity here).

**Map cycle and quit (scripted):** ending the Katabatic match through the original time-limit path cycles to DustToDust, play continues, and disconnect plus quit end with `original process shutdown completed, code=0` ([run record](validation/ipad-hardware-permissions.json)). Test harnesses must end matches through `Game.timeLimitReached()` or the game itself; a direct `CycleMissions()` call skips `AIMissionEnd()` and leaves bots holding stale navigation nodes, which checked memory then correctly stops. That invalid route likely explains the earlier isolated null guest-PC crash.

**What remains:** about 14% of the game thread waits in the original QoL frame-latency fence (`glClientWaitSync` on the previous frame), which looks GPU/Simulator bound and is intentionally unchanged. CPU time is now spread across real emulation work: x87 arithmetic in SoftFloat/Bochs (~14%), remaining hand-written software brackets in the GL staging path and SDL bridge (~10%), translated code and helpers. Physical-iPad frame rate and touch-driven play are still unmeasured for this build.

Rejected along the way, with evidence: inline grouped checks ([patch](archive/experiments/inline-grouped-guard-rejected.patch); no FPS change, code size doubled), sticky readers (slower in isolation), GL4ES batching (`LIBGL_BATCH`, no change).

## Architecture and code map

The pinned original Tribes 2 x86 image, TribesNEXT/QoL DLL and required plugins are translated ahead of time to native ARM64. A checked guest address space preserves their 32-bit pointers. Finite, validated code revisions implement the original client's runtime patching; dynamically written x86 bytes are not executed as ARM code.

- `scripts/`: input preparation, lifting, validation, checked-module builds and app packaging. Full app entry points are `build-ipad-engine.py` and `build-macos-engine-app.py`; `build-apple.py` is the smaller diagnostic shell.
- `port/core/`: native Windows/service bridges, guest CPU arithmetic, memory/lifetime/thread and input/audio/network implementations.
- `port/apple/`: app integration, UIKit controls and GL4ES/ANGLE/SDL graphics context integration. Mobile graphics reach Metal through ANGLE.
- `config/runtime/`: explicit memory contracts and reviewed source identities. Never bypass a rejected proof/hash to make a build pass.
- `inputs-manifest.json`, `dependencies.lock.json`: selected input identities and dependency pins.
- `docs/validation/`: tracked compact evidence/screenshots. `generated/integration-review/`: much larger ignored local logs, profiles, experiments and receipts.

Keep native AOT execution and checked memory, permission, lifetime and concurrent publication semantics intact. Active guest threads make separate CPU/stack captures unsuitable as coherent whole-memory snapshots.

## Last validated code and experiments

The last gameplay-validated runtime is **`d73c01c`** ("Vectorize checked MMX arithmetic for Apple ARM64") plus the opt-in frame-timing logger, which ran through two complete offline matches on 10 October. The current Simulator build of this source is `9941fde3fc6afeb959a237b5a42156a6b3d3ff75ce5c68a5dc190c614d2b0b62` (104,349,424 bytes), exercised through an offline match on 10 October. An earlier build of the same source was `c99ee37b…`; the two are not byte-identical and the cause was not isolated, so record the hash of whatever you run. The MMX receipt hash `310ce4fc…` predates the logger. The device bundle has not been rebuilt since.

| Commit | Change / evidence |
| --- | --- |
| `d73c01c` | MMX native vectors: 56,096 x86-oracle cases including final checked module; 8,052 cases across 671 original instruction sites; checked CPU/x87 tests; actual offline touch match and clean Quit |
| `6e9d957` | Bounded masked native-object access proof; no demonstrated game-speed gain |
| `83604da` | 21+ minute session, two map loads, inventory/station equipment, clean Quit; zero abort breakpoint hits |
| `43c990a` | Bounded straight-line memory groups 8→16 operations; measured but nondeterministic frame-delivery improvement |
| `ef4d47b` | Original iPad worker/DLL shutdown completes |
| `967e053`, `33154f5`, `52ee95b` | WAV/MP3 repair, x87 fog-store repair, inventory touch/keyboard routing |

Archived, unshipped experiments: [inline-grouped-guard-rejected.patch](archive/experiments/inline-grouped-guard-rejected.patch) (rejected above) and [guest-page-census-diagnostic.patch](archive/experiments/guest-page-census-diagnostic.patch) (opt-in counter used for the census). Earlier require-inlining, single-page shortcuts, grouped-continuation, cold/noinline reader paths and sticky readers also lacked whole-game gains.

**Repeatable offline route without clicking:** at the original menu, press the grave/tilde key to open the game console, type `LaunchGame("Host");` and Return, toggle the console off and on (the HOST pane takes keyboard focus), then type `tryToStartHostedGame();` and Return. These are the original script functions behind LAN GAME and START GAME; saved prefs select Katabatic, Capture the Flag and two bots. Close the console before timing.

## Known risks and unfinished work, in priority order

1. **Hardware-enforced checked memory: acceptance.** Done for the Simulator route above, including touch-driven play of every control and a scripted map cycle and quit. Remaining: the touch Launch → Quit menu route; the device build and physical-iPad measurement (Extended Virtual Addressing, signal-based fault handling with and without a debugger); then make `--hardware-permissions` the default once those pass. The bracketed build remains the reference.
2. **Physical iPad acceptance.** Check current device/signing availability once. Earlier checks had neither a connected device nor development identity. Device packaging needs provisioning retaining Extended Virtual Addressing; the engine reserves a 4 GiB virtual range. This is not a claim of 4 GiB physical usage, nor proof it fits the device's memory budget. See [Development](DEVELOPMENT.md).
3. **Reproducible build: public verification.** Done on this Mac from a clean clone. Remaining: make the repository public, run the GitHub workflow (7 GB runner; the build peaked at 6.3 GB in one process) and PadMint's `scripts/player-check.sh`, tag a release, then add the PadMint catalog entry. Other Xcode versions may lift differently; the build stops with a clear message if a base digest differs.
4. **Stability and complete controls.** The earlier isolated null guest-PC abort most likely came from cycling missions without the original end-of-match cleanup (see Map cycle and quit); the original end-of-match route works. Not proven to be the same event. Test real simultaneous touch, station navigation by touch, controller and background/resume.
5. **Mac and iPhone.** Mac performance; iPhone and Mac layouts are verified in the Simulator and on this Mac; do not present the diagnostic controls shell as the full game.
6. **Miles DirectSound fallback.** If the renderer fails to start, the game sets up Miles' DirectSound mixer, which reaches an untranslated instruction (`0x2114e23b` in `Mss32.dll`). DirectSound `SetCooperativeLevel` from Miles' thread and `Get/SetSpeakerConfig` now work; translating the mixer path needs a new lift, patches and receipts. Normal play uses the native OpenAL provider and never reaches it.
7. **Fidelity and product completion.** Compare representative terrain/interiors/water/weapons/players/vehicles, HD sampling and audio against the pinned reference. Resolve online test scope with the human only when ready; no automatic joining. Full original product acceptance remains outstanding.

## Build and workspace handoff

Players and fresh clones: `scripts/build-tribespad.sh [--platform device|macos|simulator] [--games tribes2,starsiege]` (PadMint and the GitHub workflow build both games). After changing translated inputs or receipts, or a file a receipt hashes (such as `config/runtime/external-memory-contracts.json`), re-record patches with `scripts/translation-patches.py make` (see Reproducible build) and commit them; a fresh clone otherwise stops at the app step.

Workspace: `~/GitHub/tribespad`. **Do not delete it after pulling GitHub.** At wrap-up, ignored `ref/` was about 3.7 GB and `generated/` about 250 GB. Git contains source, input manifests and selected validation evidence, not the game assets, complete translated inputs, native dependencies or all logs. A fresh clone builds with the one command above. Do not commit those ignored directories wholesale.

Prepared baseline used by the full build: `generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe`. Preserve it until a clean reproduction succeeds. Local Python is `.venv/bin/python`; pinned LLVM was `/opt/homebrew/Cellar/llvm@20/20.1.8/bin`. Xcode SDK 26.5 was used; accidentally selecting the Command Line Tools SDK 27 previously caused TAPI incompatibilities. Component tests that link for macOS need both `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer` and `SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"`; otherwise `clang` still picks the Command Line Tools SDK and fails with a TAPI error. A change to `guest_memory_checks.c` or `ExternalMemoryPass.cpp` invalidates eleven receipts the iPad builder checks; rerun cpu-host-checks, host-local-checks, native-cpu-checks, sse-host-locals, x87-host-checks, mobile-gpu-timing, code-revision (both modes), mobile-gpu-sync, sdl-io, guest-threads and guest-access-groups (about two minutes).

For the existing prepared workspace (inspect active processes first):

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export SDKROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)"
.venv/bin/python scripts/build-ipad-engine.py --angle --cpu-host-checks \
  --bundle-assets --build-only
# Output: generated/ipad-engine/TribesPad.app

export SDKROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
.venv/bin/python scripts/build-ipad-engine.py --platform device --angle \
  --cpu-host-checks --build-only
# Output: generated/ipad-engine-device/TribesPad.app (not provisioned)
```

MMU-enforced variant (recommended for play testing; output `generated/ipad-engine-hw/TribesPad.app`):

```sh
export SDKROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)"
.venv/bin/python scripts/build-checked-bochs.py --platform simulator --hardware
.venv/bin/python scripts/build-ipad-engine.py --angle --hardware-permissions \
  --bundle-assets --build-only
```

Component checks for that mode: `clang -DTRIBES_HARDWARE_PERMISSIONS port/core/guest_memory_checks.c tests/hardware_permissions_test.c` (run on macOS and with `xcrun simctl spawn`), plus `tests/float80_widen_test.c` against SoftFloat.

Boot only one Simulator. Install/launch `com.tribespad.full-engine.ipad` on its current UDID; don't rely on old process IDs. Start **LAN GAME → HOST → Katabatic → two bots → START GAME**. After loading, enable **Touch controls** and Fire to spawn. Leave the match, then **Launch → Quit** to exercise normal cleanup. [README](../README.md) has the control layout. The Mac app is `generated/macos-engine-hw/TribesPad.app` (`build-ipad-engine.py --platform macos --angle --hardware-permissions --bundle-assets --build-only`); the older bootstrap-packaged Mac app is superseded. See [Development](DEVELOPMENT.md) for packaging and native input-helper commands.

Source/policy changes intentionally invalidate proof receipts. Refresh the specific affected tests, not every historical suite. For example, the interrupted context diagnostic required `test-sdl-io.py` and `test-guest-threads.py` after the reviewed-source identity changed; both passed normal/checked/ThreadSanitizer checks before the experiment build. Returning to validated source can invalidate those experimental receipts again. A build failure identifying stale proof is actionable dependency information, not permission to bypass it.

## Accuracy against the original game

A reference recording now compares TribesPad with the original Windows game. [tests/sim_reference_trace.cs](../tests/sim_reference_trace.cs) runs the same offline scenario in both (Katabatic, fixed start position and velocity, server-side state on every tick) and [scripts/compare-reference-trace.py](../scripts/compare-reference-trace.py) compares the traces. Results ([record](validation/reference-trace-katabatic.json)):

- Each version repeats its own runs exactly.
- Against the original Tribes2.exe (no QoL) under CrossOver, TribesPad's flight is bit-identical and its landing velocities match to every printed digit. Positions differ by about 1 cm from the first ground contact, which is where QoL's collision patch changes the result; the reference lacks QoL, so this is expected but not yet proven.
- QoL itself does not start under Wine on this Mac (SDL3 video init under Whisky, an IFC22 fault under CrossOver), so a QoL-to-QoL comparison is the next accuracy step. Running the reference under CrossOver: copy ref/control into the tribespad-reference bottle, add $pref::AcceptedEULA = 1; to base/prefs/ClientPrefs.cs, append the trace script to base/autoexec.cs, and launch Tribes2.exe -nologin in the foreground.

## App icon and branding

- **App icon:** `port/apple/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png`, a full-bleed square with no corners, border or transparency; the OS applies its own mask. `scripts/app_icon.py` compiles it with actool for the iPad (Simulator and device) and Mac app builds and returns the Info.plist icon keys. Verified on the iPad Simulator home screen and the macOS Dock.
- **README logo:** `docs/images/tribespad-logo.png`, a transparent hexagon emblem used only in documentation.
- The Mac app built by `build-ipad-engine.py --platform macos` compiles the same icon with actool.

## Suggested next session

1. Read this document and [ipad-hardware-permissions.json](validation/ipad-hardware-permissions.json). Inspect Git status and confirm the Simulator binary hash before relying on it.
2. Build the hardware variant (see Build), host Katabatic, show the touch controls with the controller button and tap Fire to spawn; record frame timing (TRIBES_FRAME_TIMING=1) and check the reference trace still matches (tests/sim_reference_trace.cs, scripts/compare-reference-trace.py).
3. Profile again before optimizing. After the x87 exact path, the largest CPU items are the remaining GL staging memory checks (stage_array), indirect-call lookup (tribes_lookup) and draw-data copies; the per-frame fence wait is GPU-side in the Simulator. Map loading (~70 s) is unprofiled.
4. Report what runs, measured frame rate, what failed and the next decision. Do not claim completion from component tests or rendering alone.
