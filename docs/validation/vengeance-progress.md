# Vengeance progress ledger

One entry per check, newest last. See [the goal loop](../VENGEANCE-GOAL-LOOP.md).

| Date | Milestone | Command | Inputs / layout | Result | First failure | Next |
| --- | --- | --- | --- | --- | --- | --- |
| 2026-10-31 | V0 | `scripts/vengeance/prepare.py` | archive `22180a81…`, 7,752 files | Pass: every digest matched; truncated archive rejected | — | V1 |
| 2026-10-31 | V1 | `scripts/vengeance/layout.py` | layout v1 | Pass: 27 modules, no overlaps, imports resolve | 87 Windows functions missing for UCC; no DLL loader | M1 |
| 2026-10-31 | M1 | `scripts/vengeance/lift.py`, then `scripts/vengeance/build_engine.py --run --show-script-log -- Core.HelloWorldCommandlet -intparm=7 -strparm=TribesPad` | layout v1; 7 UCC modules | Pass: all six DLL attaches succeed; UCC runs the UnrealScript commandlet ("Hello, world!", parameters parsed), exit code 0 | — (`ucc help` lists commands from Editor.dll, outside the UCC build) | M2 |
| 2026-10-31 | M3 | `lift.py` (all 27), `build_engine.py --target client --run -- -log` | layout v1; 26 client modules; `.kpjaig`/`.bokuht` data (lift-fixups.json) | Pass: all 27 modules lift; client links (389,204 dispatch entries); all 19 static DLL attaches succeed; TV_CD_DVD.exe entered at 0x10916b0c | user32.dll!FindWindowExA (single-instance check) | M4: Unicode windows (WinDrv), D3D9 on Metal, DirectInput8, audio |
| 2026-10-31 | M4 | `lift.py` (36 modules: +IGEffectsSystem, IGSound/VisualEffectsSubsystem, Tyrion, TribesTV, UWeb, Editor, ScriptCompiler, PerforceWrapper), `build_engine.py --target client`, then the engine headless with `VENGEANCE_CAPTURE` | layout v1 + 9 run-time modules; 63 lift fixups (import and relocated-address operands, bt/bts, fnstenv/fldenv, high-byte forms) | Pass: the original main menu renders on Metal and animates (13,500 frames in 150 s, about 90 per second; captures kept under generated/vengeance/evidence); Direct3D 9 device on the 1024x768 game window; ALAudio opens OpenAL Soft and plays; DirectInput 8 mouse created | Exit path: oleaut32 VariantClear (WMI clean-up), fixed next | M5: input into the menu, start SP-Spaceship |
