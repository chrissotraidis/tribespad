# Reproducible build

Goal: a fresh clone builds TribesPad from the official free downloads with one
command, so that PadMint's **Build** button (Apple Silicon Mac with Xcode) and a
GitHub Actions macOS runner (for iPad-only players) can make a personal copy.
No game data or translated game code is ever committed or published.

The current build works only in the original development workspace, which keeps
about 260 GB of prepared inputs, evidence and old builds under the ignored
`ref/` and `generated/` folders. This document tracks replacing each of those
inputs with a recorded, repeatable stage.

## How the current build was measured

`scripts/audit-build-inputs.py` runs any build script with Python audit hooks
and records every file it opens and every program it starts. For the MMU-enforced
iPad Simulator build (`build-ipad-engine.py --angle --hardware-permissions
--bundle-assets`) it recorded about 2,400 input files and 644 tool runs (clang,
opt, llvm-nm, codesign) in these groups:

| Group | Examples | Count |
|---|---|---|
| Compiled runtime and support objects (rebuilt by the build itself) | `generated/ipad-engine-hw/{guarded,support}`, `generated/bochs-math/checked-simulator-hw` | ~1,700 |
| Pinned third-party sources and prebuilts | Bochs, SoftFloat, ANGLE, GL4ES, OpenAL Soft, zlib-ng | ~400 |
| Repository sources | `port/`, `scripts/`, `tests/`, `config/` | ~180 |
| Third-party libraries built for the target | SDL3, GL4ES, OpenAL, zlib-ng, curl/LibreSSL | ~100 |
| **Translated game sources** | `generated/srw/run-…-ifc22/…/bootstrap-published-ntdll-probe`, Miles modules, `generated/integration-review/*-engine` | ~110 |
| Test receipts the build checks | `generated/integration-review/<test>/result.json` | ~15 |
| Tools | Homebrew `llvm@20`, Xcode | — |

## Stages

| # | Stage | Produces | Status |
|---|---|---|---|
| 0 | **Game inputs** — `scripts/prepare-game-inputs.py` | `ref/control`, `ref/qol`, `ref/product` | **Done.** Verified from an empty folder with macOS's Python 3.9: downloads the official installer and TribesNEXT client, checks SHA-256, builds pinned REWise, and reproduces all 170 game files and 22 TribesNEXT files byte for byte ([config/game-inputs.json](../config/game-inputs.json) holds only paths and digests). |
| 1 | Tools | SRW/llasm static recompiler (`toolchains.lock.json`), LLVM 20 passes (`generated/tools/*-pass`) | Sources pinned; a from-scratch build script is still needed. |
| 2 | Third-party runtime libraries | SDL3, GL4ES, ANGLE (prebuilt, pinned), OpenAL Soft, zlib-ng, curl/LibreSSL, SoftFloat, Bochs math | Each has a build script; not yet run from a clean clone. |
| 3 | Lift | SRW output for `Tribes2.exe`, `IFC22.dll` and the Miles DLLs/codecs | Scripts exist; run directories are dated and hand-selected. |
| 4 | **TribesNEXT patch preparation** | The ~110 translated sources: finite code revisions for the runtime patches the QoL client applies, routes, hooks and continuations | **Hardest stage.** About 40 `*_runtime_inputs.py` modules read 13.5 GB of evidence in 40 `generated/integration-review` folders, produced over weeks by capture and freeze scripts that ran parts of the original QoL code. Plan: record the small, game-independent part of that evidence (addresses, patch order, block lists, expected hashes) in the repository and regenerate the translated code from a fresh lift, instead of re-running every capture. |
| 5 | Receipts | Component tests the build checks by source hash | For player builds, check source hashes against committed receipt summaries rather than re-running long tests. |
| 6 | App and packaging | Device `TribesPad.app`, unsigned IPA, Mac app | Exists (`build-ipad-engine.py --platform device`); IPA packaging and entitlements notes still needed. |
| 7 | PadMint and GitHub runner | `padmint.json`, a workflow that builds the IPA in a player's own fork | Not started; depends on stages 1–6. |

The game cannot be shipped or built on an iPad: iOS only runs code signed at
install time, so the translated game must be compiled on a Mac (the player's own,
or a GitHub runner in the player's own fork) before installing.

## Licensing of inputs

Tribes 2 is a free download from its rights holders but is not public domain, so
TribesPad never redistributes it. Stage 0 downloads it from the official sources
on the player's computer. The optional HD texture pack comes from a third-party
link and is only used when the player supplies it (`--hd-textures`).
