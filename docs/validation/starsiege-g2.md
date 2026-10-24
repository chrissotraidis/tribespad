# Starsiege: Tribes G2 record (24 October 2026)

Gate G2 in [STARSIEGE-TRIBES-PLAN.md](../STARSIEGE-TRIBES-PLAN.md): lift 1.41 and reach the main menu. **The original main menu renders** from the translated 1.41 `Tribes.exe` on this Mac, headless: no window was shown and nothing appeared on screen or in the Dock.

![Starsiege: Tribes 1.41 main menu, rendered headless by TribesPad](starsiege-main-menu-headless-2026-10-24.png)

## What runs

- `scripts/starsiege/build_engine.py` puts the lift in TribesPad's original-address model, compiles it with TribesPad's native Windows services and the same SDL backend, GL helpers, GL4ES and ANGLE as Tribes 2's Mac app, and links `port/starsiege/starsiege_engine.c`. `--run` starts it on an isolated copy of the game with `STARSIEGE_HEADLESS=1` (hidden SDL window, SDL as a background app) and writes frames to PNG.
- From the game's own `console.log`: MSVC start-up, the `TRIBES_LOCK` instance check, OpenGL set-up against GL4ES, CPU detection, all client scripts and modules, `gui/MainMenu.gui.cs`, `Client Initialized`, GGConnect and OpenSSL start-up. The menu then draws every frame: 93 textured quads under `glOrtho(0, 800, 600, 0)` with per-control scissoring. It ran 90 seconds without a stop (261,000 frames, uncapped because the drawable is hidden).

## What was added (Starsiege only; Tribes 2's pinned sources and receipts are unchanged)

| Area | Where |
|---|---|
| Engine entry: one image at 0x400000, TEB, threads, files, display, SDL allocator | `port/starsiege/starsiege_engine.c` |
| Win32 windows: creation with WH_CBT hooks, subclassing, default procedure, geometry, focus | `port/starsiege/starsiege_windows.c` |
| WGL and GDI pixel formats on the SDL backend's GL contexts; 13 GL 1.1 entry points Tribes 2 never used | `port/starsiege/starsiege_gl.c` |
| Display modes (`EnumDisplaySettingsA`, `ChangeDisplaySettingsA`) and window metrics | `port/starsiege/starsiege_display.c` |
| `HeapSize`/`HeapValidate`, socket buffer sizes, ANSI hardware registry, critical sections counting re-entry like Windows | `starsiege_heap.c`, `starsiege_winsock.c`, `starsiege_registry.c`, `starsiege_critical_section.c` (each includes or mirrors the unchanged TribesPad source) |
| Interlocked calls, named semaphore, gamma ramp, `FormatMessageA` 2000, `fsincos` and more | `starsiege_win32.c`, `starsiege_x87.c` |
| Bindings: new services and reasoned overrides | `config/starsiege/bindings.json` |

Reported as not present: `ScriptGL.dll`, `rmdgfx.dll`, `mscoree.dll`, `dbghelp.dll`, and named pipes such as `\\.\pipe\IAPlayer.1337.0`. DirectInput and DirectSound became present later the same day (below).

## Toward G3: local game (same day)

- A scripted route (\`build_engine.py --route\`, run copy only) creates a player profile and calls the game's own \`createServer("Raindance", false)\`. The server loads Raindance (CTF, Blood Eagle and Diamond Sword), the client connects over LOOPBACK and is accepted, then it drops right after the Loading screen. The server then moves to its next mission. The drop has no reason and no script kick, so it is in the engine's connection handling. It is the next thing to find.
- Start-up took 25 s because TribesPad's list-based heap is O(n) per call and Starsiege's C runtime sends every malloc to it. \`starsiege_fast_heap.c\` (size classes and a hash table, with the original heap as fallback for runtime blocks) brings the longest start-up frame to 1.3 s.
- The translated game now builds at \`-O1\` (about two minutes; \`STARSIEGE_OPT=-O0\` for quick iteration). \`fxam\` and \`fldl2e\` are implemented. The run copy drops the installer's stale \`temp/serverTempData*.cs\`.

## Input, cursor and sound (same day, later)

The menus now take a mouse and keyboard. Headless and scripted, with no window shown: Play Game opens Player Setup, typing a name into the New Player box and pressing Return creates the player, and the armor preview renders.

![Player Setup after a scripted click, typed name and Return](starsiege-player-setup-input-headless-2026-10-24.png)

- **Input is DirectInput.** `console.cs` calls `inputActivate(keyboard0)` and `inputActivate(mouse0)`; window messages only drive the editors. Tribes 2's native DirectInput root (`guest_dinput.c`, unchanged) creates the devices from its own keyboard and mouse catalog. `port/starsiege/starsiege_input.c` includes it and adds the methods Tribes 2 never needed: `SetCooperativeLevel`, `Acquire`/`Unacquire`, `GetDeviceState`, `GetDeviceData`, `SetEventNotification` and `Poll`. Keys and mouse motion come from SDL events through the backend's `tribes_sdl_poll_event`, every frame and on each device read, with SDL scancodes mapped to DirectInput scan codes. The SDL allocator routes moved from `0xfffd0000` to `0xfffc0000`, because that address is the native token for `DirectInputCreateA`.
- **The game's own cursor.** The 1.41 canvas draws its cursor only after a `WM_MOUSEMOVE` tells it the pointer is over the window; it then follows DirectInput motion. The host now sends that message when the window first shows, and again for motion while the game has released the mouse, kept inside the window because an Apple screen has nothing beside the game. 1.41 also turns `$pref::politeGui` on when it creates its window, which stops the canvas from ever taking the pointer and leaves the cursor to the Windows desktop. TribesPad turns it off right after the window exists (`tribespad_defaults` in `build_engine.py` adds one line after `newObject(MainWindow, ...)` in `console.cs`). That is a TribesPad default, like WASD for Tribes 2.

  ![The game's hand cursor over Play Game](starsiege-menu-cursor-headless-2026-10-24.png)
- **Sound.** `dSound.dll` now resolves to Tribes 2's DirectSound provider on an SDL/CoreAudio device. The game opens a 48 kHz stereo device and its primary buffer, and "error opening sound device" is gone. Playback itself is unchecked until a match runs.
- Headless checks script input with `STARSIEGE_INPUT=file` (`<frame> key <DIK> down|up`, `<frame> move dx dy`, `<frame> button n down|up`, `<frame> wheel n`) and pick capture frames with `STARSIEGE_CAPTURE_FRAMES=a,b,c`. `STARSIEGE_TRACE_INPUT=1` logs device calls.

## Local game: still dropped

With input and sound in place the local game behaves as before. The server accepts the player and the client shows the Loading screen, then the engine drops the connection without a reason ("Connection to server lost: Server went down") and the empty server cycles to its next mission. The scripts only react to the drop (`Server::onClientDisconnect`, then `Server::refreshData` and `Server::nextMission`). The translation was checked for silent gaps: its 192 untranslated sites are SSE and environment instructions that stop the engine loudly if reached, and none was reached. The cause sits inside 1.41's own connection checks, which this work does not take apart; how to proceed there is an owner decision.

## Still open for G2 and next

- A visible Mac window, which needs the owner's go-ahead on this Mac.
- An offline match with bots (G3), blocked on the drop above.
- Diagnostic logging (file opens, GL lookups, window calls) stays on during bring-up.

Reproduce: `.venv/bin/python scripts/starsiege/lift_preview.py --no-compile`, then `.venv/bin/python scripts/starsiege/build_engine.py --run` (with `DEVELOPER_DIR` and `SDKROOT` set as in docs/HANDOFF.md).

