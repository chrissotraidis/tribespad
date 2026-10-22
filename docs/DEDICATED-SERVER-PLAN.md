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

