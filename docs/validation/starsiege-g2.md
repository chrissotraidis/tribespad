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

Reported as not present: `ScriptGL.dll`, `rmdgfx.dll`, `mscoree.dll`, `dbghelp.dll`, `dInput.dll` (1.40 falls back to window messages), `dSound.dll` (sound is G3 work), and named pipes such as `\\.\pipe\IAPlayer.1337.0`.

## Still open for G2 and next

- Input: keyboard and mouse as window messages from the host (the console shows `unknown device: keyboard0` until then).
- A visible Mac window, which needs the owner's go-ahead on this Mac.
- Sound (DirectSound provider) and an offline match with bots: G3.
- Diagnostic logging (file opens, GL lookups, window calls) stays on during bring-up.

Reproduce: `.venv/bin/python scripts/starsiege/lift_preview.py --no-compile`, then `.venv/bin/python scripts/starsiege/build_engine.py --run` (with `DEVELOPER_DIR` and `SDKROOT` set as in docs/HANDOFF.md).

