# Tribes: Vengeance goal loop

Written 31 October 2026. This is the working loop for taking Tribes: Vengeance
from verified files to a campaign that plays on the iPad from TribesPad's
picker. The engineering detail behind each milestone is in
[VENGEANCE-IMPLEMENTATION-PLAN.md](VENGEANCE-IMPLEMENTATION-PLAN.md).

## Goal

The Vengeance campaign plays on the iPad Pro from the picker. That means the
original menus, a new profile, the opening cutscene and SP-Spaceship, with
movement, skiing, jetpack, firing, AI and sound, using touch and a controller.
A save survives quitting and relaunching. Tribes 2 and Starsiege: Tribes keep
working in the same app. Online play (V6) and a public release (V7, PadMint)
are later goals and need the owner's approval.

## Ready on this Mac

| Prerequisite | State |
| --- | --- |
| Game files | `ref/vengeance/game`: 7,752 files, 10.09 GiB, every digest checked (`scripts/vengeance/prepare.py`) |
| Archive | `ref/downloads/TribesVengeance.zip`, SHA-256 checked; download link answered 31 October |
| Module layout | `config/vengeance/layout.json` v1, checked (`scripts/vengeance/layout.py`) |
| Translation tools | SRW, SRW-tribes, llasm and memory passes in `generated/tools`; LLVM 20.1.8; pefile |
| Graphics, SEH and input sources | HaloPad `c4aad68` in `ref/halopad` (D3D9, shader, SEH, DirectInput) |
| Mac and iOS device libraries | Built (Tribes 2 and Starsiege builds use them) |
| Simulator libraries | Built 31 October: SoftFloat, Bochs math, SDL, GL4ES, OpenAL, zlib-ng, HTTP, checked memory |
| Touch-controls test | `scripts/test-mobile-controls.py` passes again |
| Game source reference | Vengeance dev kit UnrealScript and native class headers, `~/CodexWork/tribespad-vengeance-refs/TribesRevengeance-Source` at `8e13d7a`; read only, never committed |
| Devices | iPad Pro 12.9-inch (6th generation) paired; iPhone 14 paired (was locked) |
| Disk | About 600 GiB free on the Mac |
| Windows reference | Parallels Windows 11 VM and Wine 9.0 present; **using them needs the owner's go-ahead** |

Still to check by hand: free space on the iPad (Settings > General > iPad
Storage) before the first large install.

## The loop

Each turn of the loop:

1. **Read the state.** Open the progress ledger
   (`docs/validation/vengeance-progress.md`) and find the current milestone and
   the last recorded failure.
2. **Run the milestone's check.** Use the command in the table below, from a
   clean build of the changed parts.
3. **If it passes,** record the evidence, commit, open or update the
   milestone's PR, and move to the next milestone.
4. **If it fails,** take the *first* failure (a stop names the missing import,
   instruction, helper or guest address). Classify it: loader, Windows function,
   instruction, CRT/exception, file path, renderer, audio, input, game logic.
   Make the smallest correct fix, with a focused test when the fix has
   semantics worth pinning (an instruction, a Windows call's edge cases, an
   unwind path). Then go back to step 2.
5. **Regression gate** at every milestone and after any change to shared
   runtime code: Tribes 2 and Starsiege still build and start (Mac headless),
   `test-mobile-controls.py` passes, and both pickers render.
6. **Write the ledger entry:** date, milestone, command, input and layout
   digests, result, first failure and the next experiment.

### Milestones and their checks

| # | Plan step | Done when | Check |
| --- | --- | --- | --- |
| M1 | V2 | UCC's seven modules translated, loaded in dependency order and running natively; `ucc help` prints its commands and exits 0 | `scripts/vengeance/build_engine.py --target ucc`, then run the UCC host |
| M2 | V2 | A bounded package experiment: UCC loads Core/Engine packages with the null renderer, or the record says why that route cannot reach gameplay | UCC commandlet log |
| M3 | V2b | The client's entry and every reference into `.kpjaig` traced and classified; the client translated and running to its first real stop | Client host log, first stop |
| M4 | V3 | The original main menu draws on the Mac (offscreen PNG), with D3D9 on Metal, DirectInput and OpenAL started | Headless run, PNG and log |
| M5 | V3 | SP-Spaceship loads; the player moves, skis, jets and fires; sound plays (Mac) | Scripted input run, frames, audio log |
| M6 | V4 | Through the normal menus: new profile, opening cutscene, SP-Spaceship, save, quit, relaunch, load | Scripted route log and frames |
| M7 | V5 | iOS: the picker card reads Dev build and starts Vengeance; touch and controller profile; ··· menu; lifecycle; Simulator first, then the iPad with the documented backup | Simulator screenshots, iPad log |
| M8 | V5 | The owner plays the opening mission on the iPad; frame times, peak memory, heat and load time recorded | Owner's confirmation and device log |

### Rules

- A milestone counts only with its evidence. A build, a Simulator run or a
  translator exit code is not device gameplay.
- Missing Windows functions, instructions and helpers are explicit stops, never
  silent success. No blanket exclusion of `.kpjaig` or other code.
- Tribes 2 and Starsiege behavior stays unchanged; the regression gate catches it.
- Game files and translated output stay in ignored folders; the repository gets
  scripts, manifests, host code and evidence only.
- iPad installs use the documented backup and read-back.

### When to stop and ask

- The same failure survives three honest attempts, or a milestone needs a
  design change the plan does not cover (for example runtime-generated x86 code).
- Anything that needs the owner: running the original game in the Windows VM or
  Wine, joining public servers, PadMint changes, releases or public posts.
- The iPad or iPhone is locked or unavailable when a device check is due.

## Objective for a Codex goal

> Make Tribes: Vengeance's campaign play on the iPad Pro from TribesPad's picker
> (menus, opening mission, movement, ski, jet, fire, AI, sound, touch and
> controller, save and reload), following docs/VENGEANCE-GOAL-LOOP.md milestone
> by milestone, with evidence in docs/validation/vengeance-progress.md, Tribes 2
> and Starsiege unchanged, and stopping for the owner at the listed points.
