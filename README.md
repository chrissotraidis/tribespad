# TribesPad

Tribes 2 on iPad, iPhone and Mac, using the original game and TribesNEXT client.

TribesPad translates the original x86 game ahead of time into native ARM64 code
and supplies the Windows, graphics, audio and network services it expects on Apple
platforms. The aim is the original game, offline bot matches and compatibility with
the existing Tribes 2 community, with touch and controller support on mobile.

**In development.** The Mac app renders and runs offline matches, but performance
is currently too slow for comfortable play. The full iPad Simulator app now loads an offline
Katabatic match with bots and accepts touch movement, aim, weapons, firing and jets.
The Metal backend also renders interiors and supports touch death/respawn.
Latest iPad Simulator captures measure about **5.3–5.9 FPS**, so performance is
still the main blocker. Slow startup, input delay and a terrain gap shared with Mac
also need work.
There is no playable iPad or iPhone release yet.

![Original Tribes 2 running through Metal with iPad touch controls](docs/validation/ipad-fan-batching-2026-10-09.png)

## What works

- **Original Mac menus and rendering.** The game runs in a signed native Mac app,
  with corrected mouse targeting on Retina displays. [Mac screenshot](docs/validation/macos-retina-katabatic-2026-10-08.png).
- **Offline matches on Mac.** Katabatic loads with two bots. Movement, aiming and
  disc firing have been exercised through native keyboard and mouse input.
- **Internet server listings.** The original browser lists 26 community servers
  and refreshes their information. No online match has been joined in these tests.
- **iPad app integration.** The full original engine builds and starts in the iPad
  Simulator. Its checked GL4ES bridge passes real pixel readback and context
  lifecycle tests, including the original GUI shaders, perspective projection and
  native GPU fences. The [original map on iPad](docs/validation/ipad-katabatic-2026-10-08.png)
  renders, and [touch gameplay](docs/validation/ipad-touch-gameplay-2026-10-08.png)
  moves, aims, switches weapons, fires and jets in the offline match. Touch death
  and [respawn](docs/validation/ipad-angle-respawn-2026-10-09.png) also work.
  The new [Metal backend](docs/validation/ipad-angle-offline.json) passes the native
  clipped-triangle regression and the checked bridge suite with 36 expected guard
  failures. Physical-iPad execution is still unverified.

## iPad controls

Tap **Touch controls** in an offline match to show the overlay.

| Control | Action |
| --- | --- |
| Move pad | Move with the original ESDF bindings |
| Look pad | Drag to aim |
| Fire + Aim | Hold to fire; drag to aim while firing |
| Jet | Hold to jet; release to recover energy |
| Ski / Jump | Original jump action |
| Weapon | Switch weapon |
| Menu | Open the original Escape menu |
| Keyboard | Show the onscreen keyboard for a selected text field |

Movement, look, weapons, fire and jet have been exercised in the Simulator.
Skiing, simultaneous gestures and physical-device responsiveness still need validation.
The bundled app preserves its original warrior and video preferences between launches.

## Current work

- Improve measured runtime performance. Verified ARM64 object caching speeds up
  rebuilds and avoids 179 redundant support-code compilations. CPU-register and
  native-helper proofs reduce the optimized executable from 158.6 MB to about
  104 MB while preserving guest-memory checks. The latest compiler correction
  reduces permission groups in the main engine by 12%, retaining every checked
  access. A small per-thread dispatch cache also avoids repeated table searches
  while retaining publication checks. Expanding triangle fans before Metal removes
  a measured renderer stall. Build-time graphics availability and direct finite
  lookup remove repeated name comparisons. A cheaper atomic reader reservation
  preserves concurrent permission checks and improves the latest captures from
  4.2–4.3 to [5.3–5.9 FPS](docs/validation/ipad-reader.json). Memory checks remain
  the largest measured runtime cost; the app still needs more performance work.
- Fix visible rendering gaps and reduce startup/input latency in the iPad app.
- Integrate and refine touch, controller, keyboard and mouse input in real gameplay.
- Verify HD textures, sustained performance and physical-device behavior.

Offline testing and server discovery are the current multiplayer-related checks;
authenticated online play remains unverified. Detailed results and remaining issues
are in [status](docs/STATUS.md) and the [execution plan](docs/EXECUTION-PLAN.md).

## Build from source

The development build needs Xcode, pinned LLVM tools, prepared game inputs and
translated images. A clean checkout is not yet a one-command build.

In the prepared workspace, build and package the Mac app with:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
.venv/bin/python scripts/test-qol-native-bootstrap.py \
  generated/srw/run-20261003T190924-ifc22 --probe-ntdll --enter-engine
.venv/bin/python scripts/build-macos-engine-app.py
open generated/integration-review/macos-engine-app/TribesPad.app
```

For the full iPad app, boot one iPad Simulator and use its identifier:

```sh
export SDKROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)"
.venv/bin/python scripts/build-ipad-engine.py --angle --cpu-host-checks \
  --bundle-assets --simulator-device <UDID>
```

`--angle` uses a pinned ANGLE Metal renderer behind the existing GL4ES bridge.
Its first build downloads the verified iOS frameworks; subsequent builds reuse
them. Omitting the flag selects the earlier EAGL backend.

Use `--cpu-host-checks` for the optimized Simulator and device builds. This optional
mode requires current passing runs of `scripts/test-cpu-host-checks.py`,
`scripts/test-host-local-checks.py`, `scripts/test-native-cpu-checks.py`,
`scripts/test-sse-host-locals.py` and `scripts/test-x87-host-checks.py`. It keeps
guest-memory checks, validates the native register span once per translated block,
and removes redundant checks only for proven in-bounds native stack, TLS and private
global accesses. Native SIMD and x87 helpers also validate the full CPU object once, then
use proven bounds for indexed register fields. Pointers read from those objects
and all guest operands still receive guest-memory checks.

Use `--build-only` to preserve a running test session. The development touch overlay
uses the original ESDF bindings and is enabled with **Touch controls**. Its native
event tests pass, and the controls have been exercised in the original offline match.
Tap an original text field, then **Keyboard**, to enter text. The focused field stays
above the onscreen keyboard; Return dismisses it. Sensitivity, simultaneous gestures
and sustained performance still need refinement.

For a self-contained device build:

```sh
export SDKROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
.venv/bin/python scripts/build-ipad-engine.py --platform device --angle \
  --cpu-host-checks --build-only
```

The device app bundles game data and creates a writable profile in Application
Support on first launch. The optimized device executable is about 104 MB and passes ad-hoc signature verification.
It still needs Apple development signing and physical-iPad validation; this is not an installable release. Use `--bundle-assets` to exercise
that same storage layout in the Simulator. Default Simulator builds use workspace
assets for faster iteration. The separate Apple controls diagnostic does not contain
the full game.

Builds cache verified translations and optimized ARM64 objects. Changes to the
checked-call policy still run the guard pass; byte-identical guarded output can
reuse its verified native object. Unchanged builds have reused 407 of 409 objects
on Simulator and device. Original-engine support compilation reads
symbols directly from checked bitcode, avoiding 179 throwaway native objects before
the final external-call guard and code-generation pass.

Build details, native input helpers and component checks:
**[Development guide](docs/DEVELOPMENT.md)**.

## Game files and references

Game data is not included in this repository. Downloads, textures and generated
translations stay under ignored `ref/` and `generated/` directories.
[Input hashes](inputs-manifest.json) identify the selected game and TribesNEXT files;
[dependency pins](dependencies.lock.json) include the
[HaloPad / ProjectReach](https://github.com/chrissotraidis/projectreach) reference.
Free download availability does not grant redistribution rights.

See the [product target](docs/Tribes2-Online-HD-Target.md) for the intended experience.
Problems: [open an issue](https://github.com/chrissotraidis/tribespad/issues).
