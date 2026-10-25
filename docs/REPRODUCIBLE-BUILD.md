# Reproducible build

One command builds TribesPad from a fresh clone, on any Apple Silicon Mac with Xcode,
and it is what PadMint's **Build** button and the GitHub runner workflow run:

```sh
scripts/build-tribespad.sh                    # iPhone and iPad: build/output/TribesPad.ipa
scripts/build-tribespad.sh --platform macos   # Mac: build/output/TribesPad-mac.zip
scripts/build-tribespad.sh --platform simulator
```

Every build also downloads PlayT1's Starsiege: Tribes 1.41 installer (checked against
`config/starsiege/inputs.json`) and puts both games in the picker; `--games tribes2` builds
Tribes 2 alone. If Starsiege cannot be built, the build says so and finishes with Tribes 2.

Prerequisites: Xcode with the iOS platform, and
`brew install llvm@20 cmake ninja sevenzip zstd`. No game data or translated game
code is committed or published; every stage downloads or derives its inputs on the
player's Mac and checks them by SHA-256.

## Stages

| Stage | Script | What it does |
|---|---|---|
| Python | `.venv` | Apple's Python 3.9 with `scripts/requirements.txt` |
| Game files | `prepare-game-inputs.py` | Downloads the official Tribes 2 installer and the TribesNEXT client, builds pinned REWise, extracts `ref/control`, `ref/qol` and `ref/product` and checks all 192 files ([config/game-inputs.json](../config/game-inputs.json)) |
| Sources | `fetch-sources.py` | Fetches the pinned SR (SRW, llasm), HaloPad helpers, LDC, OpenAL Soft, zlib-ng, Bochs, GL4ES and SDL ([dependencies.lock.json](../dependencies.lock.json)); curl, LibreSSL, SoftFloat and ANGLE are fetched and checked by their build scripts |
| Tools | `build-tools.py` | Builds SRW, llasm, the Tribes lifter patch and the two LLVM 20 passes from the locked sources (checked against [toolchains.lock.json](../toolchains.lock.json)) |
| Translation | `translation-patches.py bases`, `apply` | Analyses and lifts `Tribes2.exe`, `IFC22.dll`, `Mss32.dll` and 11 Miles codecs; then recreates the 166 translated sources and receipts the engine was tested with (below) |
| Libraries | `build-*.py` | SDL3, GL4ES, OpenAL Soft, zlib-ng, curl/LibreSSL, SoftFloat and checked Bochs math for the target |
| Game profile | `apple_game_profile.py` | The game folder the app bundles, without the optional Discord SDK |
| App | `build-ipad-engine.py` | Compiles and checks the full engine with hardware memory permissions and the GL4ES → ANGLE (Metal) renderer, then signs the app ad hoc; the device build carries the Extended Virtual Addressing entitlement |
| Package | | Unsigned IPA (iPhone and iPad) or a zipped Mac app |

Each stage skips work it has already verified, so a failed build resumes where it stopped.

## How the translation is recreated

The engine links about 2 GB of translated code and test receipts that were produced over
weeks of capture and verification. Re-running that history on a player's Mac is not
practical, and shipping it would distribute game code. Instead the build works like a
ROM patch:

1. The player's Mac lifts its own downloaded binaries with the pinned lifter. The lifted
   outputs ("bases") must match the SHA-256 digests in
   [config/translation-patches.json](../config/translation-patches.json); a mismatch
   stops the build.
2. [patches/translation](../patches/translation) holds zstd binary differences from
   those bases to each file the engine needs (166 files, 3.5 MB in total). Receipts that
   contain no game code are stored compressed without a base.
3. Every recreated file must match its recorded SHA-256 before the app build uses it,
   and the app build then repeats its own receipt checks.

Maintainers record new patches after changing translated inputs:

```sh
TB_AUDIT_LOG=/tmp/inputs.jsonl .venv/bin/python scripts/audit-build-inputs.py \
  scripts/build-ipad-engine.py --angle --hardware-permissions --bundle-assets --build-only
.venv/bin/python scripts/translation-patches.py bases
.venv/bin/python scripts/translation-patches.py make /tmp/inputs.jsonl --keep
```

Files a compiler reads through include paths are not seen by the audit and must be
added with `--extra` (the prepared folder's `.inc` files are recorded this way).

## Verified so far

- From an empty clone on this Mac with Apple's Python 3.9: tools built from source
  produced byte-identical bases, all 166 files were recreated, and the build produced an
  IPA that passes PadMint's package check and a Mac app.
- The Simulator app from that clean clone was installed fresh and played an offline
  Katabatic match (spawn, touch controls, jetting). The Mac app played an offline match.
- PadMint's player check (Apple's Python 3.9, empty PadMint folder, fresh copy of the
  committed source) built the IPA in 32 minutes with a clean checkout afterwards.
- Peak memory of one build process was 6.3 GB; the build used about 10 GB of disk.

Not yet verified: the GitHub runner workflow and PadMint's check of a published release
(both need the repository to be public), other Xcode versions, and installation on a
physical device. The steps for launch day are in [Public launch](PUBLIC-LAUNCH.md).

## Licensing of inputs

Tribes 2 is a free download from its rights holders but is not public domain, so
TribesPad never redistributes it. The build downloads it from the official sources on
the player's computer. The optional HD texture pack comes from a third-party link and is
only used when the player supplies it.
