# TribesPad handoff — 10 October 2026

## Read this first

**The original game runs offline on Mac and in the iPad Simulator, now near 30 FPS in the Simulator with the MMU-enforced build. It is not yet a finished, accepted Apple port.** The primary engineering problem is runtime performance, followed by physical-device validation and reproducible packaging. This is not currently blocked on another missing startup API or on permission to use this Mac.

The 10 October performance review (below) replaced guesswork with a native frame baseline and a profile, rejected one guard optimization with evidence, and identified the one structural change large enough to matter. The earlier goal remains paused and there is no continuation automation. No full-engine app or build process was left running.

Start here instead of replaying the enormous checkpoint histories in [STATUS](STATUS.md) and [EXECUTION-PLAN](EXECUTION-PLAN.md). Their old “next,” “blocked,” and live-process statements are historical. The heartbeat's repeated GetQueuedCompletionStatusEx boundary was superseded long ago.

**Current human test scope: offline bot matches and server discovery only. Do not join public matches, register accounts or authenticate automatically.** The [PRD](Tribes2-Online-HD-Target.md) describes the broader online/HD product target; it does not override the user's later restriction. Online acceptance remains deferred and unverified, rather than silently satisfied by discovery.

## What actually works, and what does not

| Area | Observed result | Remaining acceptance |
| --- | --- | --- |
| macOS original engine | Menus, offline Katabatic with bots, native movement/aim/disc fire, Retina mouse targeting | User hands-on review, responsive performance, fullscreen and regression testing after shared-runtime changes |
| iPad Simulator original engine | Metal rendering; offline Katabatic, touch spawn/move/aim/weapon/fire/jet/respawn; saved profile | Comfortable performance, simultaneous gestures, skiing, sustained play, full visual/audio fidelity |
| iPad lifecycle/inventory | Two offline map loads in one 21+ minute session; equipment selection and station changes; original Quit exits code 0 | Station setup used diagnostic console positioning; a fully touch-navigated route remains untested |
| Physical iPad | ARM64 device bundle builds and passes ad-hoc signature checks | Development provisioning, installation, actual launch, RAM/thermal/performance and input testing; no physical gameplay evidence |
| iPhone | Shared mobile code and a separate controls diagnostic exist | Full game integration and acceptance; full-engine builder currently sets `UIDeviceFamily=[2]` (iPad only) |
| Controller | Extended-gamepad mappings and native component checks | Physical controller gameplay on an actual device |
| Server discovery | Mac original browser displayed 26 community servers and refreshed information | Mobile discovery acceptance; discovery does not prove joining/authentication |
| Online matches | No accepted authenticated session | Deferred under current user scope |
| HD/audio | 235 distinct captured upload-input buffers match selected HD pixels; fog/x87 and WAV/MP3 defects repaired | GPU sampling/scene fidelity, audible quality and speaker output; data correctness is not sensory acceptance |

Key evidence, each with its own limitations:

- [Mac offline controls and server browser](validation/macos-retina-input.json), [Mac screenshot](validation/macos-retina-katabatic-2026-10-08.png), [HD upload inputs](validation/macos-hd-upload-inputs.json).
- [Latest iPad component/build/gameplay results](validation/ipad-mmx-vectors.json), [gameplay screenshot](validation/ipad-mmx-vectors-gameplay-2026-10-09.png).
- [Longer reload/inventory session](validation/ipad-offline-reload-inventory.json), [memory-group comparison](validation/ipad-memory-groups.json).

## Performance: the central unresolved problem

**Current result (10 October, MMU-enforced build):** offline Katabatic, two bots, observer view in the iPad Air 11-inch (M4) Simulator runs at **27.5–29.3 FPS** with worst frames of 57–221 ms, against 6.0–7.0 FPS for the bracketed build on the same route. The map now loads in about 70–80 seconds instead of 221. Evidence: [hardware permissions run log](validation/ipad-hardware-permissions.json); the earlier baseline and rejected experiment are in [frame timing and guard review](validation/ipad-frame-timing-guard-review.json). Measured with the opt-in `TRIBES_FRAME_TIMING=1` swap logger (CPU-side submission, not GPU completion). Run-to-run variation on this shared Mac is about 8%.

**What changed, in order of effect:**

1. **MMU-enforced checked memory** (`--hardware-permissions`): guest loads and stores run natively. Every 16 KiB host page whose four 4 KiB guest permissions agree gets exactly that protection; a mixed page, or a writable page holding published code, is narrowed and its faults are decoded, checked against the exact table and published intervals, then aborted or emulated through a private alias and resumed. Faults on pages the checker did not narrow still stop. A census found 0.47 mixed-page accesses per million in gameplay. 6.75 → 19.8 FPS; binary 104 → 64 MB; build 13 → 7 minutes.
2. **Lighter GL entry and O(1) indirect-target check:** pure GL calls keep binding/owner checks but no longer copy and compare every SDL public buffer per call (still revalidated at the next SDL entry before SDL uses them); indirect calls use a frozen bitmap of the same 523,444 targets instead of a binary search.
3. **ANGLE stall removal:** Metal has no triangle fans or line loops, so ANGLE generated their indices per draw from a capped pool and waited for the GPU when it ran out (~6.5 ms per frame). The GL4ES context now streams client arrays into its own fenced ring buffers and expands fans/loops into indexed triangles/lines (`TRIBES_GL_STREAMING=0` disables it).
4. **Exact x87 loads without SoftFloat's thread-local environment**, verified bit- and flag-identical for all 2^32 binary32 inputs and 200 M binary64 cases.
5. **Texture uploads through a fenced pixel-unpack buffer ring:** `glTexSubImage2D` previously copied synchronously into Metal (23% of samples in the Simulator under `MTLSimTexture replaceRegion`). It now stages through a ring of `GL_PIXEL_UNPACK_BUFFER`s and falls back for unusual unpack state. Scripted-player median 24.9 → 29.5 FPS; textures visually correct.

**Player gameplay (scripted):** a TorqueScript playtest inlined into the app container's `base/autoexec.cs` hosts Katabatic, spawns, then moves, turns, fires, jets and jumps. The player view renders correctly at a median of 29.5 FPS (windows 19–43 FPS depending on scene). The run7 binary ran over 30 minutes at about 29.6 FPS with a 391 MB footprint and no crash. The hardware build also compiles for physical iPad but has not been installed (no device or signing identity here).

**Map cycle and quit (scripted):** ending the Katabatic match through the original time-limit path cycles to DustToDust, play continues, and disconnect plus quit end with `original process shutdown completed, code=0` ([run record](validation/ipad-hardware-permissions.json)). Test harnesses must end matches through `Game.timeLimitReached()` or the game itself; a direct `CycleMissions()` call skips `AIMissionEnd()` and leaves bots holding stale navigation nodes, which checked memory then correctly stops. That invalid route likely explains the earlier isolated null guest-PC crash.

**What remains:** about 14% of the game thread waits in the original QoL frame-latency fence (`glClientWaitSync` on the previous frame), which looks GPU/Simulator bound and is intentionally unchanged. CPU time is now spread across real emulation work: x87 arithmetic in SoftFloat/Bochs (~14%), remaining hand-written software brackets in the GL staging path and SDL bridge (~10%), translated code and helpers. Physical-iPad frame rate and touch-driven play are still unmeasured for this build.

Rejected along the way, with evidence: inline grouped checks ([patch](experiments/inline-grouped-guard-rejected.patch); no FPS change, code size doubled), sticky readers (slower in isolation), GL4ES batching (`LIBGL_BATCH`, no change).

## Architecture and code map

The pinned original Tribes 2 x86 image, TribesNEXT/QoL DLL and required plugins are translated ahead of time to native ARM64. A checked guest address space preserves their 32-bit pointers. Finite, validated code revisions implement the original client's runtime patching; dynamically written x86 bytes are not executed as ARM code.

- `scripts/`: input preparation, lifting, validation, checked-module builds and app packaging. Full app entry points are `build-ipad-engine.py` and `build-macos-engine-app.py`; `build-apple.py` is the smaller diagnostic shell.
- `port/core/`: native Windows/service bridges, guest CPU arithmetic, memory/lifetime/thread and input/audio/network implementations.
- `port/apple/`: app integration, UIKit controls and GL4ES/ANGLE/SDL graphics context integration. Mobile graphics reach Metal through ANGLE.
- `config/runtime/`: explicit memory contracts and reviewed source identities. Never bypass a rejected proof/hash to make a build pass.
- `inputs-manifest.json`, `dependencies.lock.json`: selected input identities and dependency pins.
- `docs/validation/`: tracked compact evidence/screenshots. `generated/integration-review/`: much larger ignored local logs, profiles, experiments and receipts.

Keep native AOT execution and checked memory, permission, lifetime and concurrent publication semantics intact. Active guest threads make separate CPU/stack captures unsuitable as coherent whole-memory snapshots.

## Last validated code and experiments

The last gameplay-validated runtime is **`d73c01c`** ("Vectorize checked MMX arithmetic for Apple ARM64") plus the opt-in frame-timing logger, which ran through two complete offline matches on 10 October. The current Simulator build of this source is `9941fde3fc6afeb959a237b5a42156a6b3d3ff75ce5c68a5dc190c614d2b0b62` (104,349,424 bytes), exercised through an offline match on 10 October. An earlier build of the same source was `c99ee37b…`; the two are not byte-identical and the cause was not isolated, so record the hash of whatever you run. The MMX receipt hash `310ce4fc…` predates the logger. The device bundle has not been rebuilt since.

| Commit | Change / evidence |
| --- | --- |
| `d73c01c` | MMX native vectors: 56,096 x86-oracle cases including final checked module; 8,052 cases across 671 original instruction sites; checked CPU/x87 tests; actual offline touch match and clean Quit |
| `6e9d957` | Bounded masked native-object access proof; no demonstrated game-speed gain |
| `83604da` | 21+ minute session, two map loads, inventory/station equipment, clean Quit; zero abort breakpoint hits |
| `43c990a` | Bounded straight-line memory groups 8→16 operations; measured but nondeterministic frame-delivery improvement |
| `ef4d47b` | Original iPad worker/DLL shutdown completes |
| `967e053`, `33154f5`, `52ee95b` | WAV/MP3 repair, x87 fog-store repair, inventory touch/keyboard routing |

Archived, unshipped experiments: [inline-grouped-guard-rejected.patch](experiments/inline-grouped-guard-rejected.patch) (rejected above) and [guest-page-census-diagnostic.patch](experiments/guest-page-census-diagnostic.patch) (opt-in counter used for the census). Earlier require-inlining, single-page shortcuts, grouped-continuation, cold/noinline reader paths and sticky readers also lacked whole-game gains.

**Repeatable offline route without clicking:** at the original menu, press the grave/tilde key to open the game console, type `LaunchGame("Host");` and Return, toggle the console off and on (the HOST pane takes keyboard focus), then type `tryToStartHostedGame();` and Return. These are the original script functions behind LAN GAME and START GAME; saved prefs select Katabatic, Capture the Flag and two bots. Close the console before timing.

## Known risks and unfinished work, in priority order

1. **Hardware-enforced checked memory: acceptance.** Done for the Simulator route above. Remaining: touch-driven play and the touch Launch → Quit menu route in the hardware build; the device build and physical-iPad measurement (Extended Virtual Addressing, signal-based fault handling with and without a debugger); then make `--hardware-permissions` the default once those pass. The bracketed build remains the reference.
2. **Physical iPad acceptance.** Check current device/signing availability once. Earlier checks had neither a connected device nor development identity. Device packaging needs provisioning retaining Extended Virtual Addressing; the engine reserves a 4 GiB virtual range. This is not a claim of 4 GiB physical usage, nor proof it fits the device's memory budget. See [Development](DEVELOPMENT.md).
3. **Reproducible build/package.** Remove dependence on unexplained local preparation; inventory required generated inputs and document regeneration. Verify source-to-bundle identity before distributing. Source backup is not a portable app release.
4. **Stability and complete controls.** Investigate the isolated null guest-PC abort from an earlier eight-operation baseline (~404 seconds). Repeated and longer sessions did not reproduce it; it is not proven fixed. Prior report: `~/Library/Logs/DiagnosticReports/Retired/TribesPad-2026-10-09-172739.ips`, if still retained. Test real simultaneous touch, skiing, station navigation, controller, background/resume and long sessions.
5. **Mac and iPhone.** Rebuild/regress Mac shared-runtime changes and get hands-on acceptance. Implement and exercise full iPhone layout/device-family support, one Simulator at a time; do not present the diagnostic controls shell as the full game.
6. **Fidelity and product completion.** Compare representative terrain/interiors/water/weapons/players/vehicles, HD sampling and audio against the pinned reference. Resolve online test scope with the human only when ready; no automatic joining. Full original product acceptance remains outstanding.

## Build and workspace handoff

Workspace: `/Users/chrissotraidis/GitHub/tribespad`. **Do not delete it after pulling GitHub.** At wrap-up, ignored `ref/` was about 3.7 GB and `generated/` about 250 GB. Git contains source, input manifests and selected validation evidence, not the game assets, complete translated inputs, native dependencies or all logs. A fresh clone is not currently a one-command reproducible build. Do not commit those ignored directories wholesale.

Prepared baseline used by the full build: `generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe`. Preserve it until a clean reproduction succeeds. Local Python is `.venv/bin/python`; pinned LLVM was `/opt/homebrew/Cellar/llvm@20/20.1.8/bin`. Xcode SDK 26.5 was used; accidentally selecting the Command Line Tools SDK 27 previously caused TAPI incompatibilities. Component tests that link for macOS need both `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer` and `SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"`; otherwise `clang` still picks the Command Line Tools SDK and fails with a TAPI error. A change to `guest_memory_checks.c` or `ExternalMemoryPass.cpp` invalidates eleven receipts the iPad builder checks; rerun cpu-host-checks, host-local-checks, native-cpu-checks, sse-host-locals, x87-host-checks, mobile-gpu-timing, code-revision (both modes), mobile-gpu-sync, sdl-io, guest-threads and guest-access-groups (about two minutes).

For the existing prepared workspace (inspect active processes first):

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export SDKROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)"
.venv/bin/python scripts/build-ipad-engine.py --angle --cpu-host-checks \
  --bundle-assets --build-only
# Output: generated/ipad-engine/TribesPad.app

export SDKROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
.venv/bin/python scripts/build-ipad-engine.py --platform device --angle \
  --cpu-host-checks --build-only
# Output: generated/ipad-engine-device/TribesPad.app (not provisioned)
```

MMU-enforced variant (recommended for play testing; output `generated/ipad-engine-hw/TribesPad.app`):

```sh
export SDKROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)"
.venv/bin/python scripts/build-checked-bochs.py --platform simulator --hardware
.venv/bin/python scripts/build-ipad-engine.py --angle --hardware-permissions \
  --bundle-assets --build-only
```

Component checks for that mode: `clang -DTRIBES_HARDWARE_PERMISSIONS port/core/guest_memory_checks.c tests/hardware_permissions_test.c` (run on macOS and with `xcrun simctl spawn`), plus `tests/float80_widen_test.c` against SoftFloat.

Boot only one Simulator. Install/launch `com.tribespad.full-engine.ipad` on its current UDID; don't rely on old process IDs. Start **LAN GAME → HOST → Katabatic → two bots → START GAME**. After loading, enable **Touch controls** and Fire to spawn. Leave the match, then **Launch → Quit** to exercise normal cleanup. [README](../README.md) has the control layout. The Mac development app path is `generated/integration-review/macos-engine-app/TribesPad.app`; see [Development](DEVELOPMENT.md) for packaging and native input-helper commands.

Source/policy changes intentionally invalidate proof receipts. Refresh the specific affected tests, not every historical suite. For example, the interrupted context diagnostic required `test-sdl-io.py` and `test-guest-threads.py` after the reviewed-source identity changed; both passed normal/checked/ThreadSanitizer checks before the experiment build. Returning to validated source can invalidate those experimental receipts again. A build failure identifying stale proof is actionable dependency information, not permission to bypass it.

## Suggested next session

1. Read this document and [ipad-hardware-permissions.json](validation/ipad-hardware-permissions.json). Inspect Git status and confirm the Simulator binary hash before relying on it.
2. Build the hardware variant (see Build), run the console route, then spawn with **Touch controls → Fire + Aim** and play for several minutes; record frame timing in player view and check clean **Launch → Quit**.
3. Profile again before optimizing: x87 SoftFloat arithmetic and the remaining GL staging brackets are the largest CPU items; the per-frame QoL fence wait is likely GPU-side.
4. Report what runs, measured frame rate, what failed and the next decision. Do not claim completion from component tests or rendering alone.
