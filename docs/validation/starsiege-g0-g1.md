# Starsiege: Tribes G0 and G1 record (24 October 2026)

Gates from [STARSIEGE-TRIBES-PLAN.md](../STARSIEGE-TRIBES-PLAN.md). Every number below came from a command run on 24 October on the maintainer's Mac. Nothing was run as Windows code, no game window was opened, and no server was joined. Translation results are measurements, not a boot.

## G0: input, rights and population

**Rights decision (owner, 24 October):** target the community 1.41 client, "the version everyone uses for online play". The 2004 freeware 1.11 is no longer the first target. The record in `config/starsiege/inputs.json` says what community sources say about 1.40's origin and that redistribution authorization has not been established; each player's build downloads the game itself, as with Tribes 2.

**Input:** PlayT1's Retro 1.41 installer, the download [playt1.com](https://playt1.com/) points new players to.

| Item | Value |
|---|---|
| URL | `https://tribes.nyc3.digitaloceanspaces.com/configs/retro_141_installerv2.exe` |
| Size, SHA-256 | 161,942,399 bytes, `5ff65763…151581` |
| Format | Nullsoft installer; 7-Zip extracts it, no Windows code runs |
| Game tree | 969 files, 234 MB, each pinned by SHA-256 |
| `Tribes.exe` | 3,031,040 bytes, SHA-256 `19cd6c9f…2d947` |

`scripts/starsiege/prepare-inputs.py` downloads, verifies, extracts and checks every file into `ref/starsiege/game`. It runs with Apple's Python 3.9 and is separate from the Tribes 2 inputs.

The same `Tribes.exe` (identical SHA-256) is inside the "Tribes 1.41 Original by Plasmatic" archive on GitHub (shaykbake/Tribes), so the executable pin holds across both community sources. That archive is not used: it contains a 2021 server log of player IP addresses.

**Population snapshot** (`scripts/research/t1query.py --all-masters`, Sat 24 Oct 00:40 EDT): all six masters answered and listed the same 29 servers; 22 players on 4 servers. Rosco's Mixtape 1999 (PlayT1.co, base, LT map) had 13; three RPG servers had 5, 3 and 1. Reported versions: 1.11 ×11, 1.1998.108x ×6, 1.30 ×5, 1.41 ×3, 1.0 ×3, 1.3.12 ×1. Together with the two 22 October snapshots (13 and 9 players), this is a small evening community centered on one base server; it does not change the plan.

**G0 passes.** A pinned, reproducible input exists and the rights question has the owner's answer.

## G1: census of the 1.41 executable

| | 1.8 (freeware) | 1.41 (target) |
|---|---|---|
| Built | Borland C++, 1999 | Microsoft Visual C++ 2005 (linker 8.0), 27 Aug 2009 |
| Relocation table | yes | none (fixed base 0x400000) |
| Static imports | 284 from 11 DLLs | 349 from 10 DLLs |
| OpenGL | loaded by name | 71 functions imported directly |
| Audio | DSOUND imported | `dsound.dll` loaded by name (`DirectSoundCreate`) |
| Input | Win32 messages | adds `dinput.dll` by name (`DirectInputCreateA`) |
| Glide, Smacker, `mem.dll` | all three | none of them |
| Game data | `.vol` archives | `.zip` archives (zlib and libpng are built in) |

1.41 is GarageGames' 2009 rebuild from source (1.40.655) with community fixes. Being MSVC-built removes the plan's Borland start-up and exception-handling risk, and Glide, Smacker and `mem.dll` drop out of scope.

**Loaded by name:** `dinput.dll`, `dsound.dll`, `iphlpapi.dll` (`GetIpAddrTable`), `mscoree.dll` (the C runtime's `CorExitProcess` check), `dbghelp` (`MiniDumpWriteDump`), `rmdgfx.dll` and `ScriptGL.dll`. The last is the community plugin loaded by `rasadhlp.dll`. TribesPad answers "not present" for plugins, as planned (G2 below).

**Coverage** (`scripts/starsiege/census.py`; classes as in the plan: A bound in TribesPad, B named in its sources, C imported by a Tribes 2 module but not provided, D new):

| DLL | A bound | B source | C T2 import only | D new |
|---|---:|---:|---:|---:|
| KERNEL32 (119) | 88 | 0 | 27 | 4 |
| USER32 (96) | 22 | 0 | 59 | 15 |
| OPENGL32 (71) | 66 | 3 | 0 | 2 |
| WS2_32 (29) | 21 | 0 | 5 | 3 |
| GDI32 (12) | 4 | 0 | 8 | 0 |
| COMCTL32 (11) | 0 | 0 | 0 | 11 |
| ADVAPI32 (5) | 4 | 0 | 1 | 0 |
| COMDLG32, SHELL32, OLE32 (6) | 0 | 0 | 6 | 0 |
| **Total (349)** | **205** | **3** | **106** | **35** |

59% of imports are already bound (1.8: 42%). The new work is the Win32 window under GL: USER32's window, message-hook and display-mode calls, and GDI32's `ChoosePixelFormat`, `SetPixelFormat` and `SwapBuffers`. Of the 35 new imports, 24 serve the built-in editor (COMCTL32 toolbars and image lists; USER32 menus, dialogs, scroll bars, accelerators and deferred window moves) and can stay logged stops. The rest are small: `glTexCoord2fv`, `glVertex3fv`, `InterlockedExchange`, `CopyFileA`, `MoveWindow`, `MessageBeep`, the Winsock 1 blocking-hook trio, and two named-pipe calls whose use is still to be found.

**Lift** (`scripts/starsiege/lift_preview.py`, the same audit, SRW with Tribes extensions, `llasm` and `clang -O0` used for Tribes 2):

| Step | Result |
|---|---|
| Audit | 659,350 instructions, 13,135 functions, 0 bad decodes, 6,529 unclassified bytes (0.3% of `.text`; 1.8 had 6.0%) |
| SRW | whole image, 2 s |
| `llasm` | 18 s |
| `clang -O0` | 39.5 MB ARM64 object in 90 s |
| Unresolved symbols | 491: the 349 imports and 142 runtime helpers, all of which already exist in TribesPad's runtime |

Corrections are recorded in `config/starsiege/lift-fixups.json` and the script, each with its evidence:

- **Sixteen-bit data mistaken for pointers.** With no relocation table, words of UTF-16 text and 16-bit tables (for example `0x00600030`) looked like code addresses. 206 such fixups are dropped by a rule (both halves under 256, next to more of the same). This is safe because TribesPad keeps the image at its original addresses.
- **Two not-code addresses and one code seed** around the exception-cleanup funclets at `0x600000`.
- **One data word** that points inside another data item is written as its original four bytes.
- **67 `seto`** sites, all MSVC's `operator new[]` size check right after a 32-bit `mul`, become "low byte = (EDX ≠ 0)". The script refuses any other shape.
- **`0x52BC4F`:** the branch taken when `DirectInputCreateA` fails jumps into code the community patch overwrote. It now traps loudly; TribesPad provides DirectInput, so it should never run.

**Untranslated: 202 sites, each a loud trap if reached.**

- 183 are SSE2 in the C runtime's math routines.
- 12 are one SSE routine in the game's own vector math, at `0x50EC7E` onward.

TribesPad's guest CPU reports x87 and MMX only, and `IsProcessorFeaturePresent(PF_XMMI64)` returns 0, so both groups should be skipped by their own checks. G2 confirms this at run time. Six more are `fprem1` ×3, `fnstenv`, `fldenv` and one `shr ecx, 7`; these are on the x87 path and get translator support in G2 when a route reaches them. The last is the deliberate DirectInput trap.

**G1 passes:** the image is unpacked, there is no self-modifying code, the import set is known, and the whole executable translates.

## Reproduce

```
scripts/starsiege/prepare-inputs.py                    # Apple's /usr/bin/python3 works
.venv/bin/python scripts/starsiege/census.py
.venv/bin/python scripts/starsiege/lift_preview.py     # about 2.5 minutes
python3 scripts/research/t1query.py --all-masters
```
Outputs go to the ignored `generated/starsiege/` and `generated/t1-population/`.

