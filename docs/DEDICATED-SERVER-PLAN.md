# Tribes 2 dedicated servers and what TribesPad could offer

Research date: 2026-10-22. Scope: read-only reading of this repository and the official game scripts, plus a limited web search; nothing was built. Labels: **Verified** (seen in a cited source or in local files), **Estimate** (judgement or prior knowledge not rechecked), **Unknown**.

Three web searches ran (Loki dedicated server, TribesNEXT on Linux/Wine, Docker images); a fourth, about Loki plus TribesNEXT shims, hit the search limit. ARM emulation reports, VPS prices, server mod pages and the master server's NAT behavior were not searched online. Those points are labelled Estimate or Unknown, or rest on local script evidence.

## Summary

TribesNEXT servers are run today as the Windows Tribes2.exe under Wine on x86 Linux, often in Docker. A native Loki Linux dedicated binary exists, but TribesNEXT account support is not provided for it, and it needs very old Linux libraries. TribesPad cannot run as a dedicated server yet. The scripts force it off, the console APIs a dedicated server uses are not implemented, and the runtime is tied to Darwin. The cheapest real step is a headless dedicated server for macOS arm64 (a Mac mini) built from the existing engine. A Linux aarch64 / Raspberry Pi server is a separate port that comes later. In-app hosting from iPad or Mac already registers with the master list, but it only works when the host can accept inbound UDP on port 28000.

## 1. How Tribes 2 dedicated servers are run today

### Windows: Tribes2.exe -dedicated

- **Verified (local game files).** The official install ships ref/control/Classic_dedicated_server.bat, which runs `start ispawn.exe 28000 Tribes2.exe -dedicated -mod Classic`. The listen-server variant runs `Tribes2.exe -online -mod Classic`.
- **Verified (local console_start.cs).** `-dedicated` sets `$LaunchMode = "DedicatedServer"`. A pure server is requested when no -mod is given. The DedicatedServer, Console, NavBuild and SpnBuild modes skip the video and GUI branch; that branch runs only for other launch modes. With `-nologin` the mode stays DedicatedServer and `console_end.cs` runs directly. Without it, the original WON login runs and `dedCheckLoginDone()` quits on failure.
- **Verified (local t2csri.vl2 from TribesNEXT_20250922_preview).** serverList.cs sends a heartbeat as an HTTP POST to `master.tribesnext.com` at `/add/<$Host::Port>[/<$Host::BindAddress>]` every `$Host::TN::beat` minutes (default 5). The master replies with `0` (added), `1` ("Your server could not be contacted. Check your IP / port configuration.") or `2` (confirmed). LAN discovery queries port 28000. serverSide.cs turns away non-TribesNEXT clients with "This is a TribesNEXT server. You must install the TribesNEXT client to play." The server's public IP comes from a `/whatismyip` lookup on the master (ipv4.cs). IPv6 is disabled in these HTTP objects.
- **Verified (Loki FAQ).** The default server port is 28000 ([faqs.lokigames.com](https://faqs.lokigames.com/tribes2faq.html)).

### Loki Linux port (2001)

- **Verified.** A native x86 Linux dedicated binary, `tribes2d`, exists. It installs from `tribes2d-wincd.run`, which needs the Windows Tribes 2 CD during installation. It launches like `./tribes2d -dedicated Katabatic CTF` and reads its configuration from `~/.loki/tribes2/base/prefs/ServerPrefs.cs` ([Loki FAQ](https://faqs.lokigames.com/tribes2faq.html); see also the [Tribes 2 modding wiki Linux page](https://modding.tribes2wiki.com/21-linux/)).
- **Verified.** The Loki binary depends on very old Linux libraries and generally does not run cleanly on current distributions without compatibility libraries or containers. If it prints only its banner and exits, the port or the old authentication/master lookups are the usual suspects. The traditional port is 28000 over UDP and TCP ([modding wiki](https://modding.tribes2wiki.com/21-linux/), [Loki FAQ](https://faqs.lokigames.com/tribes2faq.html)).
- **Verified.** TribesNEXT provides no native Linux server with TribesNEXT account support; the documented route is the Windows server under Wine ([TribesNEXT forum: Linux Server Setup](https://www.tribesnext.com/forum/discussion/762/linux-server-setup)). F9Alejandro's Docker image instead runs the Loki server patched for LAN mode ([repo](https://github.com/F9Alejandro/docker-tribesnext-server)).
- **Unknown.** Whether any third-party shim lets the Loki binary authenticate against TribesNEXT was not established this session (search limit).
- **Legal availability (Estimate).** The Loki installer needs the retail Windows CD, and Loki's Linux binaries are not part of the free official download TribesPad uses. Redistributing them is not clearly allowed. TribesPad should not depend on them.

### Wine on x86 Linux

- **Verified.** This is the documented TribesNEXT path on Debian/Ubuntu: a 32-bit Wine prefix (wine32, winetricks, Xvfb), the official installer plus the TribesNEXT patch (RC2a stable, a newer preview for modern systems), and a Windows Ruby runtime fix that RC2a expects ([TribesNEXT forum 762](https://www.tribesnext.com/forum/discussion/762/linux-server-setup), [tribesnext.com](https://tribesnext.com/)). There is also an automated Linux/Wine setup thread ([forum 4321](https://www.tribesnext.com/forum/discussion/4321/automated-server-setup-on-linux-wine)), a T2 Linux Server thread ([forum 3302](https://tribesnext.com/forum/discussion/3302/t2-linux-server)) and a community guide ([ChocoTaco1/Tribes2-Server-Setup-Guide](https://github.com/ChocoTaco1/Tribes2-Server-Setup-Guide)).

### Docker

| Project | Basis | Notes |
|---|---|---|
| [henrikhasell/Tribes2DedicatedServer](https://github.com/henrikhasell/Tribes2DedicatedServer) | Stock game + TribesNEXT 2025 QoL patch under Wine, headless | Telnet console exposed; env vars SERVER_NAME, MAP, MISSION_TYPE, MAX_PLAYERS, ADMIN_PASSWORD, TELNET_PASSWORD; image `henrikhasell/tribes2-dedicated-server`; run with `-p 28000:28000/udp` (**Verified** from search summary). Whether it publishes an arm64 image is **Unknown**, but Wine on x86 suggests amd64 only (**Estimate**). |
| [F9Alejandro/docker-tribesnext-server](https://github.com/F9Alejandro/docker-tribesnext-server) | Loki native server patched for LAN mode | **Verified** from search summary; TribesNEXT online auth and current maintenance **Unknown**. |

### Mods, admin tools, hosting

- **Verified (local).** The Classic mod ships in the official install (ref/control/Classic, with server.cs, admin.cs and serverDefaults.cs). The repo's product target names TacoServer as built on Classic 1.5.2 (docs/Tribes2-Online-HD-Target.md).
- **Estimate (not searched this session).** Other common server mods include LakRabbit, TWL/T2 competitive configs, Construction and BIOMod. Admin is usually through in-game admin, the telnet console (as in the Docker image above) and ServerPrefs.cs.
- **Estimate.** Tribes 2 servers are light. The engine is mostly one game thread, and a 1–2 vCPU x86 VPS with 1–2 GB RAM in the single-digit dollars per month is the usual tier. CPU per tick and bandwidth grow with player count and bots. Prices were not checked this session.

## 2. ARM options

- **Unknown (not searched).** No reports of a Tribes 2 server on ARM (Raspberry Pi, Oracle Ampere, Apple Silicon Linux VMs) were found this session.
- **Estimate.** Box64 with Wine (and Box86 or WoW64 for the 32-bit Tribes2.exe), or FEX-Emu with Wine, are the plausible paths. Both are JIT translators. The game is 32-bit x86 with x87 floating point, which these tools handle with varying exactness. Raspberry Pi 5's default kernel uses 16 KiB pages, which has historically caused trouble for Box86/Wine. The Oracle Ampere Always Free tier is a likely free host if it works. All of this is unverified, so the community's "it'd prob already run in wine" holds for x86 and is unproven for ARM.
- **Verified (local).** TribesPad already runs exact 4 KiB guest page permissions on 16 KiB host pages (HANDOFF.md "mixed pages"). That is the same mismatch Pi 5 kernels present, so a TribesPad Linux port would not hit that problem.

## 3. Can TribesPad's recompiled engine run headless as a dedicated server?

### What the repo shows

| Evidence | Where | Meaning |
|---|---|---|
| `$Host::Dedicated = 0;` with "A dedicated server would quit the app, so host in game." | scripts/build-ipad-engine.py (both offline and online autoexec heads) | Dedicated is deliberately forced off; the engine has never been run with -dedicated. **Verified.** |
| "a fresh profile shows the Dedicated host option ticked" | docs/HANDOFF.md (Mac row) | The HOST screen's Dedicated checkbox is an open product issue on Mac. **Verified.** |
| Tribes2.exe imports AllocConsole, SetConsoleTitleA, SetConsoleTextAttribute, GetNumberOfConsoleInputEvents, ReadConsoleInputA; IFC22.dll (QoL) imports WriteConsoleW/A, ReadConsoleW, ReadConsoleInputW, GetConsoleScreenBufferInfo, cursor/fill functions, GetConsoleMode and more | inputs-manifest.json | A dedicated server needs a Win32 console. **Verified.** |
| No implementation of those console functions in port/, config/ or scripts/ (only SetConsoleCtrlHandler in guest_console.c, with Ctrl+C dispatch noted as unfinished in archive STATUS) | rg over the repo | The first -dedicated run would hit import stops. **Verified** (absence by search). |
| Commit f11741b routes CURLOPT_POST / POSTFIELDSIZE / COPYPOSTFIELDS in guest_curl.c and allows them in ExternalMemoryPass.cpp; "Simulator: signed in, hosted Katabatic, match started." | git show f11741b | The TribesNEXT heartbeat POST (serverList.cs) now goes through. **Verified** for the Simulator; README calls internet registration "new and lightly tested". |
| IFC22.dll imports 126 SDL3 functions, including SDL_CreateWindow, SDL_GL_CreateContext, audio streams and gamepads | inputs-manifest.json | Whether QoL creates a window in dedicated mode is **Unknown**. If it does, a headless build needs SDL's dummy/offscreen video driver or no-window stubs. |
| SDL_Init on the guest side is gated on the main thread and on `application_ready` (guest_sdl_backend.c); the app enters through SDL_RunApp (sdl_main.c) | port | A headless entry still needs the SDL host lifecycle, or a separate entry. **Verified.** |
| guest_memory_checks.c fault handler uses `__darwin_mcontext64` / `__darwin_arm_thread_state64_*`; guest_dispatch.c and others use Apple-specific APIs | port/core | Linux needs a ucontext port of the fault handler and other Darwin replacements. **Verified.** |
| Precedent: Miles' DirectSound fallback reached an untranslated instruction and "needs a new lift, patches and receipts" | HANDOFF.md risk 6 | Code paths never run before can need new lift work. Dedicated-only engine paths (WinConsole, server-only ticks) are a real risk. **Verified** precedent; risk size **Unknown**. |

### Headless macOS arm64 server (Mac mini): work estimate

**Estimate: 1–3 weeks** of focused work, mostly evidence-driven fixing from session logs:

1. Launch the Mac engine with `-dedicated -mod Classic` (the macOS launcher already passes `launch_arguments`), and remove the forced `$Host::Dedicated = 0` for that profile only.
2. Implement a small Win32 console on stdin/stdout: AllocConsole, GetStdHandle (exists), WriteConsoleA/W, ReadConsoleInputA/W, GetNumberOfConsoleInputEvents, GetConsoleMode, screen-buffer and cursor calls as faithful no-ops. Map SIGINT/SIGTERM to the existing console ctrl dispatch for a clean quit.
3. If QoL still opens a window: set SDL's dummy/offscreen video driver, or gate GL creation. Disable Miles output, or keep OpenAL on a null device. The renderer path must not be needed.
4. Follow each import stop and untranslated-code stop in order. Each stop means new translation patches and receipts (scripts/translation-patches.py make).
5. Acceptance: the console shows "Heartbeat confirmed" from master.tribesnext.com. A PC client and a TribesPad client join, finish a match and rotate maps. Then a soak of 2+ hours with bots.

Without a GPU: the dedicated branch of console_start.cs skips video. Mac minis have GPUs anyway, so a headless Mac server needs no GPU work beyond not creating a window. **Estimate.**

### Native linux-aarch64 server (Pi 5, Ampere): work estimate

**Estimate: 4–8 weeks** after the Mac dedicated server works, and more if Pi-specific issues appear:

- Replace the Darwin fault-handler context, Mach/Apple calls and the Apple SDL lifecycle with Linux equivalents. Build the runtime, curl/LibreSSL, zlib-ng and a headless SDL (or none) with LLVM 20 for aarch64-linux.
- Reproducible build: the translation must still be produced from the player's own official download (the README's ROM-patch model). Shipping a prebuilt Pi server image would distribute translated game code, which the project's licensing rules forbid. Today's build needs Xcode and macOS. A Pi user would need a Mac to cross-build, or a Linux build of the lift and patch stages. Whether SRW and the lift tools run on Linux is **Unknown**.
- The guest needs a 4 GiB virtual reservation. That is fine on 64-bit Linux, including 16 KiB-page Pi 5 kernels (**Estimate**).
- Performance: TribesPad uses software x87 (SoftFloat/Bochs) with exact fast paths. On a Pi 5 a server with bots may be CPU-bound. Unmeasured (**Unknown**).

## 4. In-app hosting (listen server) from iPad and Mac

- **Verified (code/docs).** HOST works for a local game with bots. With f11741b the TribesNEXT heartbeat POST no longer crashes, and a signed-in Simulator hosted Katabatic. README: "Registering a hosted server on the internet list is new and lightly tested."
- **NAT (Estimate, strong).** The master's `1` reply ("Your server could not be contacted. Check your IP / port configuration.") shows that the master tries to reach the server's public IP and port. A host behind home NAT without port forwarding of UDP 28000 is very likely not listed. LAN play still works. Not tested against the live master this session.
- Practical limits (Estimate): cellular iPads sit behind carrier-grade NAT, so internet hosting is impossible there. iOS suspends backgrounded apps, which ends the server. iPad heat and battery drain during long hosting. Home upload bandwidth limits player count. A Mac on Ethernet with a forwarded port is a reasonable listen server.
- Community value: useful for private pickups with friends on a LAN, or for players who can forward ports. A clear in-game message mapped from the heartbeat reply (`0/1/2`) would explain failures without new networking code.

## 5. Ranked recommendation

1. **Point server hosts at the existing x86 Wine/Docker path now.** henrikhasell/Tribes2DedicatedServer with the 2025 QoL patch covers "run a server" for VPS owners at no engineering cost. A short documentation note would do it (to be written by Chris; no repo edit in this research).
2. **Build a headless macOS arm64 dedicated server from the TribesPad engine.** This reuses the proven engine and toolchain and answers "Mac mini server". It is also the control server the product target asks for in online testing. 1–3 weeks (Estimate).
3. **Polish iPad/Mac listen-server hosting.** Surface the heartbeat result and note the port-forwarding requirement. Small work, real value for LAN and friends play.
4. **Linux aarch64 / Pi 5 server, only after #2 is accepted.** It is a runtime port plus a new reproducible-build route. Meanwhile, Box64/FEX + Wine on ARM is an unverified community experiment for others to try.

## Minimal first milestone

On this Mac, from the existing workspace: start the Mac engine with `-dedicated -mod Classic -nologin` (LAN only at first) to an isolated profile. Implement only the console functions the first stops reach. Reach the server console prompt with Katabatic loaded and no window. Then join it from the TribesPad Mac or Simulator client over LAN (`QueryLANServers(28000)`). Record the binary hash, the import stops hit and the session log. Online registration (drop -nologin, sign in, "Heartbeat confirmed") is the second gate. This milestone needs no GPU work, no Linux work and no public release.

## Sources

- Loki Tribes 2 FAQ: https://faqs.lokigames.com/tribes2faq.html
- Tribes 2 modding wiki, Linux and Loki port: https://modding.tribes2wiki.com/21-linux/
- TribesNEXT forum, Linux Server Setup: https://www.tribesnext.com/forum/discussion/762/linux-server-setup
- TribesNEXT forum, Automated server setup on Linux/Wine: https://www.tribesnext.com/forum/discussion/4321/automated-server-setup-on-linux-wine
- TribesNEXT forum, T2 Linux Server: https://tribesnext.com/forum/discussion/3302/t2-linux-server
- henrikhasell/Tribes2DedicatedServer: https://github.com/henrikhasell/Tribes2DedicatedServer
- F9Alejandro/docker-tribesnext-server: https://github.com/F9Alejandro/docker-tribesnext-server
- ChocoTaco1/Tribes2-Server-Setup-Guide: https://github.com/ChocoTaco1/Tribes2-Server-Setup-Guide
- TribesNEXT: https://tribesnext.com/
- Local: README.md, docs/HANDOFF.md, docs/DEVELOPMENT.md, docs/REPRODUCIBLE-BUILD.md, docs/Tribes2-Online-HD-Target.md, scripts/build-ipad-engine.py, port/core/guest_curl.c, guest_console.c, guest_memory_checks.c, guest_sdl_backend.c, inputs-manifest.json, commit f11741b, ref/control/console_start.cs and Classic_*.bat, ref/qol/base/t2csri.vl2.


---

## Update, 24 October 2026: headless Mac server code pass

Code pass of 24 October 2026, static analysis only (pefile and capstone over the official binaries); nothing was built or run. Line numbers refer to `main` at fa8f674.

Labels: **Verified** (seen in a file, binary or disassembly cited here), **Estimate** (judgement), **Unknown**.

Builds on docs/DEDICATED-SERVER-PLAN.md (origin/main). This pass corrects or sharpens four of its points:

1. The forced "$Host::Dedicated = 0" does **not** block a command-line dedicated launch, because console_end.cs sets it back to true. What blocks one is that the host never forwards argv, plus the autoexec Offline branch.
2. QoL (IFC22.dll) reads "-dedicated" itself and then skips SDL video, audio and gamepad init. A real command line, rather than a script override, is required.
3. In dedicated mode QoL applies a **different engine code-patch sequence**. TribesPad's finite, ordered code-revision plan was captured from the client, so a server needs its own capture and publication. This is the largest cost and was not in the earlier plan.
4. Most of IFC22's console imports come from its embedded libuv and UCRT. Reporting "no Windows console" (GetConsoleMode returns FALSE) keeps them off their console paths.

---

### 1. How the Mac app starts the engine, and where arguments could go

#### Player Mac app (what build-tribespad.sh ships)
- **Verified.** scripts/build-tribespad.sh:107 runs build-ipad-engine.py --platform macos --angle --hardware-permissions --bundle-assets --build-only. The app comes from generated/macos-engine-hw/TribesPad.app (build-tribespad.sh:111).
- **Verified.** For macOS, build-ipad-engine.py links port/apple/mobile_engine_main.m, mobile_game_paths.m, sdl_main.c, gl4es_context.c, input.c and desktop_controls.c (scripts/build-ipad-engine.py:293-295). Bundled builds write only "#define TB_BUNDLED_GAME_DATA 1" into mobile-engine-launch-config.h (build-ipad-engine.py:289, 292).
- **Verified.** main() calls tribes_sdl_run_application(argc,argv,enter_engine) (port/apple/mobile_engine_main.m:145-152). SDL_RunApp calls application_main, which signals tribes_sdl_host_application_ready() and calls enter_engine (port/apple/sdl_main.c:10-12, 25-28).
- **Verified.** enter_engine discards argc/argv (mobile_engine_main.m:20). It builds storage at ~/Library/Application Support/TribesPad (lines 23-30), and tb_mobile_prepare_paths fills a fixed 8-entry vector: exe, engine.bin, qol.bin, Volume, Game, Volume/TribesPad/Temp, Capture, "engine" (port/apple/mobile_game_paths.m:18-21, 60-61). It then calls tribes_mobile_engine_main(8, ...) (mobile_engine_main.m:36).
- **Verified.** On the Mac, stderr is not redirected to a log; tb_keep_log is iOS-only (mobile_engine_main.m:125-147). Launched from Finder, stdout and stderr go wherever launchd sends them. **Estimate:** /dev/null.

#### Developer Mac app (build-macos-engine-app.py)
- **Verified.** It packages the checked bootstrap with a fixed argument vector compiled into macos-engine-launch-config.h (scripts/build-macos-engine-app.py:38-41). port/apple/macos_engine_launcher.c:17-23 redirects stdout and stderr to generated/integration-review/macos-engine-app/app-stdout.log and app-stderr.log, writes app.pid, then calls execv on TribesPadEngine with those fixed arguments. It also ignores its own argv (main(void), line 11).

#### Where the guest command line comes from
- **Verified.** The Win32 command line the game sees is fixed when the bootstrap is prepared. config/runtime/environment.json:9-12 holds "\"C:\\TribesPad\\Game\\Tribes2.exe\" -online". scripts/test-qol-native-bootstrap.py:61-65 encodes it (cp1252 and UTF-16LE) into qol_bootstrap.inc as command_line[] and wide_command_line[]. tests/qol_native_bootstrap.c:1082-1083 passes those to tribes_command_line_configure / _w (port/core/process_environment.c:81-121). Line 1088 uses sizeof wide_command_line for the PEB length.
- **Verified.** tribes_mobile_engine_main asserts argc==7||argc==8 (tests/qol_native_bootstrap.c:1011). argv[7] is a diagnostic mode word (lines 1092-1099), so there is no slot for game arguments.
- **Verified.** tests/qol_native_bootstrap.c is a C source in prepared-inputs.json and is rebuilt on every build. build-ipad-engine.py:46-47 checks only the .ll hashes, so a runtime command-line override **does not require re-preparing the images**. qol_bootstrap.inc lives in the prepared directory (-I source, build-ipad-engine.py:155).
- **Verified.** QoL hooks the engine's argument processing: a rel32 call to 0x100615b0 is planted at engine 0x561887 (IFC22 0x10061b9d-0x10061ba7). That function scans argv for -dedicated, -nologin, -discord, -developer, -mod construction and -workers (0x100615f4-0x100616f6). **Estimate:** its argv is the engine's parse of GetCommandLineA.

**Answer.** Today, "open -a TribesPad --args -dedicated" or running Contents/MacOS/TribesPad -dedicated has no effect, because argv is dropped at mobile_engine_main.m:20 and the guest command line is a compile-time constant. The smallest change that works:
- (a) The macOS host accepts an allowlisted set of Tribes 2 flags after a marker such as "--server --".
- (b) It hands the composed cp1252 text to the bootstrap through a small host hook, for example a weak tb_host_guest_command_line(), before line 1082.
- (c) The bootstrap derives the UTF-16LE copy and its length at run time, replacing sizeof at 1088.

#### How base/autoexec.cs forces "$Host::Dedicated = 0"
- **Verified.** build-ipad-engine.py writes base/autoexec.cs into the profile (lines 169-244). The online head (179-188) is: if $TribesPadOffline then $SkipLogin, $LaunchMode = "Offline", $TribesPadInternetBrowser; else $fromLauncher = true; then "$Host::Dedicated = 0;" unconditionally. The comment reads "A dedicated server would quit the app, so host in game." The app prepends "$TribesPadOffline = 1;" when Settings > Online Play is off (port/apple/mobile_game_paths.m:40-47).
- **Verified, game script order.** console_start.cs parses arguments (ref/control/console_start.cs:235-395; -dedicated at 262-265, -nologin at 387-392 keeps DedicatedServer). It runs autoexec.cs at 405, again at 457, and once more from console_end.cs:28 (base.vl2). For DedicatedServer, console_end.cs:98-107 runs enableWinConsole(true), "$Host::Dedicated = true", setNetPort($Host::Port) and CreateServer(...). That **overrides** autoexec's 0.
- **Consequences.** (i) The "$Host::Dedicated = 0" line only affects the HOST screen. With it set, GameGui.cs:1045-1050 (base scripts.vl2) or Classic/scripts/GameGui.cs:1316-1320 would call tryToLaunchDedicatedServer, which calls the engine's launchDedicatedServer. That builds "-nologin %s-dedicated %s-mission %s %s -bot %d" (Tribes2.exe strings) and calls CreateProcessA (import sites 0x561bbf and 0x5620de, still a stop), then quit(). (ii) A real -dedicated launch is broken only by the Offline branch: "$LaunchMode = "Offline"" at autoexec time replaces DedicatedServer. (iii) The autoexec also schedules TribesPadReportState every 200 ms and TribesPadSavePrefs every minute (build-ipad-engine.py:173, 208-241). Both are harmless but useless on a server.
- Required script change (Estimate, small): gate the Offline branch and the state reporter on '$LaunchMode !$= "DedicatedServer"'. Keep "$Host::Dedicated = 0" for clients. HANDOFF.md:43 notes that a fresh Mac profile shows the Dedicated box ticked; ticking it reaches the CreateProcessA stop.

### 2. Win32 console imports: imported, bound, and minimal native implementation

Method (**Verified**): pefile over ref/control/Tribes2.exe and ref/qol/IFC22.dll. Binding status came from port/llasm-runtime/win32-bindings.json and config/runtime/*.json. Stop status came from the prepared stops.ll (generated/srw/run-20261003T190924-ifc22/.../bootstrap-published-ntdll-probe/stops.ll) and the current generated/macos-engine-hw/mobile-inputs/stops.ll (bound.py).

Call sites (capstone over IAT references):
- **Tribes2.exe** (winConsole.cc region 0x554d42-0x554fed): AllocConsole 0x554d42; SetConsoleTitleA 0x554d5d; GetStdHandle ×3 at 0x554d65/70/7b; WriteFile 0x554ecf; SetConsoleTextAttribute 0x554f24/0x554f69; GetNumberOfConsoleInputEvents 0x554fc4; ReadConsoleInputA 0x554fed. Others: CRT std-handle init 0x6feacc-0x6ff184; CreateProcessA 0x561bbf/0x5620de (launchDedicatedServer).
- **IFC22.dll**: the "conout$", "uv__malloc" and "libuv-worker" strings plus the UV_THREADPOOL_SIZE handling identify an embedded **libuv**. Its Windows tty layer accounts for the cursor, fill, screen-buffer, WriteConsoleInputW, ReadConsoleW and ReadConsoleInputW users (functions 0x10083a30-0x100864d0; uv init at 0x10074540 calls 0x10080980 SetConsoleCtrlHandler and 0x10083a30). Static **UCRT** accounts for the GetConsoleMode, GetConsoleOutputCP, ReadConsoleW and WriteConsoleW sites at 0x1010f06c/0x10122b36, and SetStdHandle at 0x1010de9e. 0x100b8880 (GetStdHandle + WriteConsoleA + GetConsoleScreenBufferInfo) is a diagnostic printer. QoL's own console-ctrl handler is 0x1005fbe0 (generated/qol-console-handler.json via scripts/audit-qol-console-handler.py: events 1/2/6 handled, 0/5 declined). The libuv identification is **Estimate (strong)**.

| Import | Module | Bound? | Minimal native behaviour (stdin/stdout) |
|---|---|---|---|
| GetStdHandle | both | **Yes**: win32-bindings.json:80 → native_handles.c:202 (dup of host fd 0/1/2) | none |
| SetStdHandle | IFC22 | **Yes**: :86 → native_handles.c:207 | none |
| GetFileType | IFC22 | **Yes**: :92 → native_handles.c:212 (tty=CHAR, pipe=PIPE, file=DISK) | none (see note on tty) |
| WriteFile | both | **Yes**: :1156 → guest_file.c:505 (accepts regular, FIFO and char devices) | none; this is how WinConsole prints |
| ReadFile | both | **Yes**: :626 → guest_file.c:468 (**regular files only**, line 474) | none needed for console input |
| SetConsoleCtrlHandler | IFC22 | **Yes**: :1090 → guest_console.c:13 (registry only) | add host signal delivery (step 5) |
| AllocConsole | T2 | No (stop) | return TRUE once, then FALSE with ERROR_ACCESS_DENIED (5) like Windows; record "console allocated" so input polling turns on |
| SetConsoleTitleA | T2 | No (stop) | TRUE; optionally store the title for the log header (no terminal escape by default) |
| SetConsoleTextAttribute | both | No (stop) | TRUE no-op; optional ANSI colour only when stdout is a tty |
| GetNumberOfConsoleInputEvents | both | No (stop) | non-blocking poll(2) of fd 0. Return the count of bytes queued as one KEY_EVENT each (cap 20, WinConsole's batch); 0 at EOF; on a non-input handle, FALSE with error 6 |
| ReadConsoleInputA | T2 | No (stop) | read up to n bytes from fd 0. Emit INPUT_RECORD{EventType=KEY_EVENT, bKeyDown=1, wRepeatCount=1, uChar.AsciiChar=byte, wVirtualKeyCode: '\n'→VK_RETURN with AsciiChar '\r', 0x7f/0x08→VK_BACK, '\t'→VK_TAB}. If stdin is a tty, switch it to non-canonical, no-echo mode for the session (restore at exit), because WinConsole echoes and edits the line itself |
| GetConsoleMode | IFC22 (libuv/UCRT) | No (stop) | **FALSE, ERROR_INVALID_HANDLE (6), for every handle.** This tells UCRT and libuv there is no Windows console, so they use WriteFile/ReadFile. Needed because GetFileType reports a Terminal stdout as CHAR (native_handles.c:216), which makes UCRT probe GetConsoleMode |
| GetConsoleOutputCP | IFC22 (UCRT) | No (stop) | return 1252, matching the guest command-line encoding (environment.json:10). Expected unreachable once GetConsoleMode fails |
| WriteConsoleA/W, ReadConsoleW, ReadConsoleInputW, WriteConsoleInputW, GetConsoleScreenBufferInfo, SetConsoleCursorPosition, Get/SetConsoleCursorInfo, FillConsoleOutputAttribute, FillConsoleOutputCharacterW | IFC22 | No (stops) | **Leave as stops** (or a named abort). With GetConsoleMode failing, libuv's uv_guess_handle and UCRT should never take these paths (Estimate). If one is reached, implement it from evidence rather than guessing |
| CreateProcessA | T2 | No (stop) | optional later: allow exactly the launchDedicatedServer command line on macOS and posix_spawn the server binary. Never on iOS |

Binding mechanics (**Verified** pattern): build-ipad-engine.py:101-110 already swaps reached stops for native wrappers from a JSON config (config/runtime/first-run-file-bindings.json → generator.generate(...); each "@tbimp_NAME" stop body is removed from stops.ll). A config/runtime/console-bindings.json plus a port/core/guest_console_io.c would follow the same route. The wrappers still pass through the usual memory and external-call passes.

**Estimate:** the engine's WinConsole::process runs from the main loop only after enableWinConsole(true) (console_end.cs:100). The console stops are therefore reached only in DedicatedServer, Console, NavBuild and SpnBuild modes, which is why the client never hit them.

### 3. Does the dedicated path avoid video init? Does QoL's SDL3 layer open a window anyway?

- **Verified (scripts).** console_start.cs:1134-1156: DedicatedServer, Console, NavBuild and SpnBuild set $Con::logBufferEnabled = false and go to console_end.cs (or the login path). Only the else branch (1157-1175) calls createCanvas(), startAudio() and the OpenGL settings. console_end.cs:98-107 returns before the GUI content code at 504 (Canvas.setCursor). Lines 58-91 still exec the GUI scripts (LaunchLanGui.cs, GameGui.cs and others), defining GUI objects without a canvas. **Unknown:** whether any of that touches font or GDI paths in TribesPad before a canvas exists.
- **Verified (QoL disassembly).** At 0x100611a1-0x100611db QoL calls SDL_SetMainReady and SDL_Init(0x4000 = SDL_INIT_EVENTS). Then "cmp byte [0x1046ce6f],0; jne skip", where 0x1046ce6f is the -dedicated flag set at 0x10061606. Only when that flag is clear does it call 0x1005b290 (SDL_InitSubSystem(0x20 = VIDEO); registers 0x100499f0, the function containing SDL_CreateWindow and SDL_GL_CreateContext), 0x10055670 (SDL_AUDIO_DRIVER=directsound, SDL_Sound init) and 0x10055090 (SDL_InitSubSystem(0x3000 = HAPTIC|GAMEPAD)). SDL_CreateWindow is called only from 0x100499f0 and 0x10049f4e (the canvas path). **With -dedicated on the guest command line, QoL never creates a window.** A script-only route (autoexec setting $LaunchMode) would leave the flag clear and still initialise video and audio.
- **Verified, other dedicated-only QoL behaviour** (refs to 0x1046ce6f):
  - At 0x10011dfd it writes **5×NOP at engine 0x5614d8** and **"EB 56" at 0x5c4875** (via VirtualProtect), then jumps past the client patch group 0x10011e4f-0x1001251c.
  - At 0x10060c38 it skips a second client patch block.
  - At 0x1006099d it skips SetPriorityClass.
  - At 0x10060527 it reads $Host::TimerPeriod.
  - At 0x10015b68 it skips client T2CSRI account-state setup.
  - At 0x1006128c it runs 'exec("t2csri/serverGlue.cs");exec("console_end.cs");function dedCheckLoginDone(){...}'. TribesNEXT therefore drives the dedicated start itself, and WON login is not used.
- **Verified (host).** guest_sdl_backend.c:399-418 allows SDL_Init with EVENTS from any thread. VIDEO, AUDIO, JOYSTICK, GAMEPAD and HAPTIC require the main thread and application_ready, so events-only init fits. **Estimate:** under SDL_RunApp on macOS, no NSApplication window is created without SDL video. When launched through LaunchServices the process may still get a Dock icon, so a server bundle should set LSUIElement (or LSBackgroundOnly), or be run directly or from launchd.

#### The biggest technical risk: code revisions
- **Verified.** The runtime accepts only a finite, ordered set of guest code writes. prepare_revision and plan_matches (port/core/guest_memory_checks.c:191-230) check every planned write's preimage and its publication coverage; numbered stages have hard limits (completed_revisions<33; banks at stages 20/30/32, line 298). tribes_lookup aborts on "unpublished original guest PC" (port/core/guest_dispatch.c:44-46). HANDOFF.md:89 confirms "Finite, validated code revisions implement the original client's runtime patching."
- **Estimate (strong).** In dedicated mode QoL leaves out client patch groups and adds two new writes. The first revision whose plan no longer matches the bytes in memory would fail or abort. A server needs its own capture → plan → published-code set: the existing tools are scripts/test-qol-native-bootstrap.py --capture-patches, build-qol-capture-plan.py, build-late-patch-plan.py and archive-qol-capture.py, with the dedicated command line. It also needs a matching tribes_dispatch_permitted variant (guest_dispatch.c:31-33 already expects "build-time patch variants"). This argues for a **separate server engine binary** (its own generated/macos-server-hw output) rather than a run-time switch inside the client binary.

### 4. Product shape (Mac only)

Recommendation (**Estimate**), smallest first:

1. **Command line (first deliverable).** Run the binary directly so stdout and stdin work:
   "/Applications/TribesPad.app/Contents/MacOS/TribesPadServer" -dedicated -mod Classic [-nologin] [-mission Katabatic CTF] [-bot 4] [-serverprefs prefs/ServerPrefs.cs]
   The flags are the stock ones from Classic_dedicated_server.bat and console_start.cs:256-395. The host allowlists them and composes the guest command line. Storage is separate, at ~/Library/Application Support/TribesPad Server/, so the client's prefs, the single-instance mutex (mobile_game_paths.m:52 "only one engine instance") and caches never collide, and a client and a server can run on one Mac. "open -n -a TribesPad --args --server ..." should also work but loses the console and adds a Dock icon. For an always-on Mac mini, provide a launchd LaunchAgent example.
2. **"TribesPad Server" launch mode.** A small AppKit launcher bundled inside the Mac app (or a separate TribesPad Server.app), in the style of port/apple/TribesPad.m:42, built by build-apple.py. It has fields for server name, map and mission type, bots, password, admin password and port, written to $Host:: variables in the server profile's prefs/ServerPrefs.cs (Classic defaults: Classic/scripts/serverDefaults.cs:29-65). It also has Start/Stop buttons (SIGTERM → console CLOSE), a live log view and a status line. The client process should not host the menu item: it owns SDL's NSApp, and the project keeps one engine per process (docs/STARSIEGE-TRIBES-PLAN.md:162).
3. **Optional, later:** bind CreateProcessA for the launchDedicatedServer command line only, so the original HOST → Dedicated checkbox spawns the server binary. Keep "$Host::Dedicated = 0" on iOS.

- **Logs.** The server host redirects stdout and stderr to ~/Library/Logs/TribesPad/Server.log, keeping the previous run as Server.previous.log, whenever they are not a tty (extend tb_keep_log, mobile_engine_main.m:130-143, to the macOS server). It also keeps the game's own console.log via setLogMode if wanted.
- **Heartbeat result.** t2csri/serverList.cs echoes lines with $Host::TN::echo (default true, line 8): " - Server added to list." (0), " - Your server could not be contacted." plus " - Check your IP / port configuration." (1), " - Heartbeat confirmed." (2), and "-- Sent heartbeat to TN Master." every $Host::TN::beat minutes (default 5) (lines 63-80, 112-135). The launcher parses these lines; no new networking code is needed.
- **Port-forwarding guidance (Estimate).** Forward UDP 28000 ($Host::Port, Classic serverDefaults.cs:59) to the Mac's LAN IP, and use $Host::BindAddress (line 58) on multi-homed Macs. Allow incoming connections in the macOS firewall; an ad-hoc signature may re-prompt after each rebuild. Hosting behind carrier-grade NAT will not work. Map master reply 1 to a plain "the internet can't reach your port" message.

### 5. Step-by-step implementation plan

0. Evidence first: rerun bound.py-style checks against the current generated stops after each step.
1. **Guest command line from the host.** port/apple/mobile_engine_main.m: on !TARGET_OS_IOS, parse "--server" and the allowlisted flags, and choose the server storage root. tests/qol_native_bootstrap.c ~1082-1088: use a weak host override for command_line and wide_command_line with a run-time length. No image re-preparation (section 1).
2. **autoexec.cs.** scripts/build-ipad-engine.py:179-241: gate the Offline branch, the state reporter and the prefs timer on '$LaunchMode !$= "DedicatedServer"'. mobile_game_paths.m:40-47: never prepend $TribesPadOffline in server storage; use -nologin for LAN-only runs.
3. **Console imports.** New config/runtime/console-bindings.json and port/core/guest_console_io.c, removed from stops the same way as build-ipad-engine.py:101-110. Implement the table rows above and leave the rest as stops.
4. **Server patch plan and binary.** Capture the QoL revision sequence with the dedicated command line. Produce the server plan and published code, then build generated/macos-server-hw with --server (build-ipad-engine.py: new output dir, no ANGLE or GL4ES needed if the link allows; Estimate). Add a tribes_dispatch_permitted variant for it.
5. **Signals.** A host thread runs sigwait(SIGINT, SIGTERM, SIGHUP) and calls tribes_console_deliver_posix on its own guest thread (port/core/guest_console_callback.h:5-14). Map SIGTERM and SIGHUP to CLOSE (QoL handles it), and treat Ctrl-C at a tty as CLOSE so onExit and ServerPrefs saving run (Estimate).
6. **Logs and App Nap.** Server.log redirection. Use NSProcessInfo beginActivityWithOptions (latency-critical, idle sleep allowed) so a windowless process is not throttled (Estimate).
7. **Launcher and docs.** Add a "TribesPad Server" launcher with heartbeat status, plus a README section on command-line hosting and port forwarding.

### 6. Test plan

- **T1 headless start (Mac, isolated root).** "-dedicated -mod Classic -nologin". Expect the console prompt, the CreateServer Katabatic output and no window (macos-engine-input PID window finds none; see docs/DEVELOPMENT.md:106-125). Record the binary hash and log. stdin "echo(1+1);" prints 2; "quit();" exits 0.
- **T2 signals.** SIGTERM leads to a clean exit with prefs/ServerPrefs.cs and banlist exported.
- **T3 LAN join from a TribesPad client.** Use the Simulator or the Mac client first (separate storage). Use the owner's iPad only with permission and with the documented app-data backup. JOIN → LAN lists the server (port 28000 query); join, play CTF with bots, finish the match and confirm the map rotates.
- **T4 online.** Drop -nologin and forward UDP 28000. The log shows " - Heartbeat confirmed.", the server appears in TribesPad's internet list (port/apple/live_servers.m) and in a PC TribesNEXT client, and both can join (t2csri serverSide.cs authentication).
- **T5 soak.** 2 h or more with bots, three or more map changes, steady RSS, no aborts; count import stops and untranslated-code stops.
- **T6 regression.** The client Mac app reaches offline Katabatic unchanged; iOS builds are unaffected (server code is macOS-only).

### 7. Risks

- **R1 (high) Patch-plan mismatch.** Dedicated-only QoL writes (0x5614d8, 0x5c4875) and skipped client groups: section 3.
- **R2 (high, size unknown) Untranslated or unpublished code on server-only paths.** WinConsole (0x554ce0 region); CreateServer with no local client; server-side bot and AI ticks without a renderer; mission loading in dedicated mode; t2csri server-side certificate checks in script. Failures show up as "unsupported/unpublished original guest PC" (guest_dispatch.c:44-46, 69). Precedent: HANDOFF risk 6 (the Miles DirectSound fallback needed a new lift).
- **R3 (medium)** GUI scripts exec'd without a canvas (console_end.cs:58-91) may reach font or GDI paths not yet exercised. Unknown.
- **R4 (medium)** A Terminal stdout looks like CHAR to UCRT and libuv; GetConsoleMode must return FALSE, otherwise the unimplemented console calls are reached.
- **R5 (low-medium)** Clashes between two instances (storage, mutex, port 28000) if the client and server share a Mac. Whether the client also binds 28000 is Unknown.
- **R6 (unknown)** Server CPU cost with software x87 and bots; unmeasured.
- **R7 (low)** macOS firewall prompts, App Nap, and a Dock icon on a windowless LaunchServices launch.
- **R8 (policy)** Same rule as the client: the server is built from the player's own downloads and never distributed prebuilt (docs/DEDICATED-SERVER-PLAN.md, section 3).

**Effort (Estimate).** 2–5 weeks, revised up from the plan's 1–3 because of R1. Steps 1–3 and 5–6 are about a week. The server patch capture and publication, plus fixing whatever server-only code turns up, is the rest.

