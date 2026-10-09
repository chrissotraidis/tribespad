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

This development Simulator bundle uses absolute workspace paths. Physical-device
asset packaging, signing and actual iPad execution remain unfinished. The separate
controls diagnostic remains a component app.

The full app also includes an opt-in UIKit touch overlay. **Touch controls** shows
movement, look, fire-and-aim, jets, ski/jump, weapon, grenade, pack and inventory.
**Hide controls** releases every held action and restores menu taps; **Menu** also
sends Escape. Losing app focus, canceled touches and layout changes release held
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
