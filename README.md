# TribesPad

A port of the actual Tribes2 build25034 and selected TribesNEXT client to native ARM64 on iPad, iPhone and macOS, targeting HD textures and existing-community multiplayer.

**Not playable yet.** The signed macOS full-engine app renders the original menus and textured character model and starts the original Training mission loader through native keyboard and mouse input. Rendering and gameplay remain under integration testing. The separate mobile app contains controls and connection diagnostics; full mobile gameplay, a rendered playable map and an authenticated match remain unverified.

Read the [current status](docs/STATUS.md), [detailed project review](docs/PROJECT-REVIEW-2026-10-03.md), [execution plan](docs/EXECUTION-PLAN.md) and [product target](docs/Tribes2-Online-HD-Target.md).

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

The build needs Xcode, the pinned LLVM tools, prepared game inputs, translated images and native dependencies. A clean checkout cannot yet reproduce all of those from a single command: cached SRW baseline acquisition remains a setup gap. [Historical build commands](docs/BUILD-HISTORY.md) are retained for investigation; their older status statements are superseded by the current status.

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

## Inputs and reference

`inputs-manifest.json` records original downloads and installed module hashes. `dependencies.lock.json` pins ProjectReach/HaloPad and other references. Game binaries, generated translations, downloads and textures stay in ignored `ref/` and `generated/` directories. The active ProjectReach repository is not modified.

Free download availability does not grant redistribution rights. The current target is a reproducible personal build; no App Store or public binary release is promised.
