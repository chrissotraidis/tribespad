# Development and validation

## Full-engine integration build

In the prepared development workspace:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
.venv/bin/python scripts/test-qol-native-bootstrap.py \
  generated/srw/run-20261003T190924-ifc22 --probe-ntdll --enter-engine
```

This builds and runs the original engine/community DLL with diagnostic stops for unsupported behavior. It is not a packaged game or a successful-startup test. Inspect the actual `result.json` under the run's `native-diagnostic/checked/bootstrap-published-ntdll-probe` directory. A diagnostic process finishing does not mean the game initialized.

To package the prepared full-engine build as a signed macOS app:

```sh
.venv/bin/python scripts/build-macos-engine-app.py
open generated/integration-review/macos-engine-app/TribesPad.app
```

This development app depends on the prepared workspace assets and is not a portable distribution. See current status for the host audio workaround and the latest runtime boundary.

The build needs Xcode, the pinned LLVM tools, prepared game inputs, translated images and native dependencies. A clean checkout cannot yet reproduce all of those from a single command: cached SRW baseline acquisition remains a setup gap. [Historical build commands](BUILD-HISTORY.md) are retained for investigation; their older status statements are superseded by the current status.

## Full original engine on iPad Simulator

With the existing translated inputs and pinned simulator SDL, GL4ES, SoftFloat,
HTTP/TLS, zlib and OpenAL dependencies prepared:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
.venv/bin/python scripts/build-checked-bochs.py --platform simulator
.venv/bin/python scripts/build-native-openal.py --platform simulator
.venv/bin/python scripts/build-gl4es-mobile.py --platform simulator
.venv/bin/python scripts/generate-legacy-shaders.py --mobile
.venv/bin/python scripts/test-qol-native-bootstrap.py \
  generated/srw/run-20261003T190924-ifc22 --probe-ntdll --enter-engine --prepare-only
.venv/bin/python scripts/test-mobile-gpu-timing.py
.venv/bin/python scripts/test-mobile-gpu-sync.py
.venv/bin/python scripts/test-code-revision.py --mobile
.venv/bin/python scripts/build-ipad-engine.py --simulator-device <booted-iPad-UUID>
```

Boot only that iPad. Before relaunching, terminate only a prior instance owned by
this development task (`com.tribespad.full-engine.ipad`). The full app is written
to `generated/ipad-engine/TribesPad.app`; build metadata, launch logs, isolated
game profile and captures are beside it. Add `--build-only` to build without
installing or launching. Existing translated sources, checked IR and native
objects are verified before cache reuse. Native C sources rebuild to account for
header changes. See [status](STATUS.md) for the reached runtime failure: build
success does not imply a rendered menu or an offline match.

Without `--bundle-assets`, the development Simulator bundle uses workspace paths.
Device builds always bundle assets; see the self-contained build section below.
Development signing and actual iPad execution remain unverified. The separate
controls diagnostic remains a component app.

The full app also includes an opt-in UIKit touch overlay. **Touch controls** shows
movement, look, fire-and-aim, jets, ski/jump, weapon, grenade, pack and inventory.
**Hide controls** releases every held action and restores menu taps. A touch on
**Inventory** also hides the pads after releasing its key, leaving equipment fields
and Done available to direct taps. **Menu** remains visible when the pads are hidden,
dismisses the keyboard and sends Escape. Losing app focus, canceled touches and layout changes release held
inputs. This uses the original ESDF/default bindings, so custom key bindings need
matching adjustments. The overlay feeds native SDL events into the existing
checked event bridge; it does not write guest input state.

`.venv/bin/python scripts/test-mobile-controls.py` checks simultaneous actions,
relative aim, quick tap edges and cancellation using the native SDL event queue,
then compiles the UIKit view for device and Simulator. It does not interrupt a
running game. These component checks do not establish original touch gameplay.

The mobile renderer uses the original 18-hook layout with legacy raster fonts.
GPU elapsed-time profiling is disabled because GL4ES's query emulation measures
CPU time; rendering keeps the original pre-sample scheduling estimate. The two
shipped GUI shaders have a finite GLSL dialect translation for a single-sample
drawable. Other shader sources still go to the real compiler unchanged.

## Native Mac app input

The input helper finds the selected engine's visible window, including SDL exclusive fullscreen on layer 25, and sends real macOS HID events. Build it with the active Xcode SDK:

```sh
mkdir -p generated/tools
xcrun clang -fobjc-arc -O2 -Wall -Wextra -Werror \
  -framework AppKit -framework CoreGraphics \
  scripts/macos-engine-input.m -o generated/tools/macos-engine-input
```

Replace `PID` with the launched engine's process ID from its `app.pid` file. `window` reports the actual owned window without sending input; `click` uses fractions of its bounds. These are desktop test actions, not guest-state injection.

```sh
generated/tools/macos-engine-input PID window
generated/tools/macos-engine-input PID key 36 0.8  # Return held for 0.8s
generated/tools/macos-engine-input PID click 0.5 0.5
generated/tools/macos-engine-input PID aim 30 -10
generated/tools/macos-engine-input PID fire 0.3
generated/tools/macos-engine-input PID jet 0.5
```

Use movement/fire/jet only after gameplay renders. Shipped defaults use ESDF, Space to jump, mouse aim and left/right buttons for fire/jet; verify the active configuration in game. Event delivery alone does not prove a gameplay action.

## Apple control and server-query diagnostic

```sh
.venv/bin/python scripts/build-apple.py --platform macos
.venv/bin/python scripts/build-apple.py --simulator YOUR_SIMULATOR_UUID
```

This separate shell links two translated routines, displays input actions and probes community servers. It does not link the full game, log in or join a match. The builder refuses a second booted simulator. Device signing and physical-game acceptance remain unfinished.

## Current multimedia checks

```sh
.venv/bin/python scripts/test-sdl-hints.py
.venv/bin/python scripts/test-sdl-display.py
.venv/bin/python scripts/test-sdl-sync.py
.venv/bin/python scripts/test-sdl-tls.py
.venv/bin/python scripts/test-sdl-audio.py
# Run mobile tests sequentially on the same sole booted simulator:
.venv/bin/python scripts/test-sdl-audio.py --simulator-device YOUR_SIMULATOR_UUID
.venv/bin/python scripts/test-sdl-display.py --simulator-device YOUR_SIMULATOR_UUID
.venv/bin/python scripts/test-sdl-sync.py
.venv/bin/python scripts/test-sdl-tls.py --simulator-device YOUR_SIMULATOR_UUID
```

Audio tests exercise actual CoreAudio silent playback and worker-thread logging. They do not establish that original game sound or the complete application works.

### Self-contained iPad build

`scripts/build-ipad-engine.py --platform device --build-only` writes the full checked
ARM64 app to `generated/ipad-engine-device/TribesPad.app`. It includes the prepared
original images, loader inputs and isolated offline game profile. No workspace path
is required at runtime. `--bundle-assets` enables the same layout in Simulator builds.
First launch copies the profile into Application Support; later launches preserve
saved settings and refresh immutable loader inputs. These reproducible files are
excluded from iCloud backup. `scripts/test-mobile-game-paths.py` tests install,
preservation, loader refresh and recovery, and compiles both entry configurations
for Simulator and device without interrupting a running game.

The output is ad-hoc signed for build verification, not provisioned for installation.
Development signing must retain `port/apple/TribesPad.entitlements` and use a profile
with Apple's [Extended Virtual Addressing capability](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.extended-virtual-addressing).
The checked original engine reserves a 4 GiB virtual address range. This entitlement
requests additional address space; it does not establish that the game fits the
physical device's memory budget. Physical-device launch and memory acceptance remain
unverified. Game data stays local and ignored by Git.

For native Simulator gestures, the owned-window helper also supports
`<pid> press <x> <y> <seconds>` and
`<pid> drag <start-x> <start-y> <end-x> <end-y> <seconds>`.
Coordinates are normalized to the visible Simulator window; gestures use real
native mouse events, which Simulator converts to touches. These actions do not
write guest memory.

## Optimized iPad build

Use `--angle --cpu-host-checks` for the current optimized Simulator and device
builds. ANGLE uses the pinned Metal frameworks; its first build downloads verified
inputs and subsequent builds reuse them. Omitting `--angle` selects the earlier
EAGL backend.

The optional `--cpu-host-checks` mode requires current passing runs of
`scripts/test-cpu-host-checks.py`, `scripts/test-host-local-checks.py`,
`scripts/test-native-cpu-checks.py`, `scripts/test-sse-host-locals.py` and
`scripts/test-x87-host-checks.py`. It retains guest-memory checks, validates the
native register span once per translated block, and removes redundant checks only
for proven in-bounds native stack, TLS and private global accesses. Native SIMD
and x87 helpers validate the full CPU object before proven register-field accesses.
Pointers read from those objects and all guest operands still receive checks.

Changed guard policies always rerun the checker. Byte-identical checked bitcode can
reuse its hash-verified optimized ARM64 object. Cached builds have reused 407 of 409
objects on each platform. Original-engine support compilation reads symbols from
checked bitcode, avoiding 179 throwaway native objects before the final external-call
pass. These changes reduce the executable from 158.6 MB to about 104 MB; those are
executable sizes, not total bundles with game data.

Runtime improvements include bounded access grouping, finite dispatch caching,
triangle-fan expansion before Metal, finite graphics lookup and per-thread reader
tracking. Every guest range, page-permission and published-code check remains.
See [the measured reader-slot result](validation/ipad-reader-slots.json) for the
8.5–8.7 FPS Simulator recordings and their limits. Component microbenchmarks do not
establish original-game speedups or physical-device acceptance.
