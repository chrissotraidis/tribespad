# TribesPad handoff — 10 October 2026

## Read this first

**The original game runs offline on Mac and in the iPad Simulator. It is not a finished, comfortably playable Apple port.** The primary engineering problem is runtime performance, followed by physical-device validation and reproducible packaging. This is not currently blocked on another missing startup API or on permission to use this Mac.

The user requested an independent review and reorientation, a clear handoff, and merging the work into `main`. Development is stopped at this handoff. The existing goal is paused, not complete. The `build-playable-tribespad` automation was checked during wrap-up and no longer exists. No full-engine app or build process was found running during wrap-up.

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

Recent Simulator screen recordings deliver roughly **6–9 FPS**, with visible input delay and several-minute loading. Latest two 30-second fixed observer recordings measured **5.84 and 7.37 FPS**. These are screen-recording measurements, not physical-iPad frame rates. The original in-game FPS display has previously been stale and must not be trusted without validation. Mac gameplay also had unacceptable performance in its recorded checkpoint; rendering alone is not playability.

A bounded memory-group change measured 7.39→8.80 FPS in one same-view comparison. Subsequent native-array proof and MMX changes did **not** establish additional whole-game gains. Live bots/snow, host load and Simulator presentation make the samples variable. The latest observed map load completed by 211 seconds, but compilation overlapped early loading, so that is not a startup benchmark.

Native samples point to checked memory access, host-span validation and reader/publication synchronization as major costs. Sample counts across worker threads are not frame-time percentages. MMX helpers became about 55% faster in isolation, but this did not translate into a demonstrated gameplay gain. Do not keep optimizing tiny helpers simply because an isolated benchmark improves.

**Reviewer decision needed:** assess whether this AOT/runtime design can reach the PRD's sustained 60 FPS target on the intended hardware while preserving correctness. Establish a trustworthy whole-game baseline and a bounded performance plan before more compatibility expansion. There is no supported completion estimate yet.

## Architecture and code map

The pinned original Tribes 2 x86 image, TribesNEXT/QoL DLL and required plugins are translated ahead of time to native ARM64. A checked guest address space preserves their 32-bit pointers. Finite, validated code revisions implement the original client's runtime patching; dynamically written x86 bytes are not executed as ARM code.

- `scripts/`: input preparation, lifting, validation, checked-module builds and app packaging. Full app entry points are `build-ipad-engine.py` and `build-macos-engine-app.py`; `build-apple.py` is the smaller diagnostic shell.
- `port/core/`: native Windows/service bridges, guest CPU arithmetic, memory/lifetime/thread and input/audio/network implementations.
- `port/apple/`: app integration, UIKit controls and GL4ES/ANGLE/SDL graphics context integration. Mobile graphics reach Metal through ANGLE.
- `config/runtime/`: explicit memory contracts and reviewed source identities. Never bypass a rejected proof/hash to make a build pass.
- `inputs-manifest.json`, `dependencies.lock.json`: selected input identities and dependency pins.
- `docs/validation/`: tracked compact evidence/screenshots. `generated/integration-review/`: much larger ignored local logs, profiles, experiments and receipts.

Keep native AOT execution and checked memory, permission, lifetime and concurrent publication semantics intact. Active guest threads make separate CPU/stack captures unsuitable as coherent whole-memory snapshots.

## Last validated code and interrupted experiment

The last gameplay-validated implementation commit is **`d73c01c`**, “Vectorize checked MMX arithmetic for Apple ARM64.” This handoff changes documentation and archives an experiment; runtime sources are restored to that commit.

| Commit | Change / evidence |
| --- | --- |
| `d73c01c` | MMX native vectors: 56,096 x86-oracle cases including final checked module; 8,052 cases across 671 original instruction sites; checked CPU/x87 tests; actual offline touch match and clean Quit |
| `6e9d957` | Bounded masked native-object access proof; four redundant dispatch-cache checks removed, unknown/guest accesses still checked; no demonstrated game-speed gain |
| `83604da` | 21+ minute session, two map loads, inventory/station equipment, clean Quit; zero abort breakpoint hits |
| `43c990a` | Bounded straight-line memory groups 8→16 operations; retained individual permissions and call/control/atomic barriers; measured but nondeterministic frame-delivery improvement |
| `ef4d47b` | Original iPad worker/DLL shutdown completes |
| `967e053`, `33154f5`, `52ee95b` | WAV/MP3 repair, x87 fog-store repair, inventory touch/keyboard routing |

Validated executable SHA-256 identities from the MMX receipt:

- Simulator: `310ce4fc7d4c6b6bd5ea5d342122243ba42e1afcc350ed93deee9f8ae6f16894` (104,349,424 bytes).
- Device: `9df24ab2787839cde4738651a70418d5828005eded7d065eb0d55ae7759f8a99` (104,283,392 bytes).

An opt-in `TRIBES_FRAME_TIMING=1` native-swap logger was then built and launched to the menu, but **offline timing validation was interrupted**. It measures swap submission intervals, not GPU completion. The exact unfinished diff is preserved in [native-frame-timing-unvalidated.patch](experiments/native-frame-timing-unvalidated.patch); it is **not applied to shipped source**. Local logs under `generated/integration-review/native-frame-timing/` report Simulator and device output paths. Existing generated bundles/receipts may therefore contain this experiment and must not be assumed to match the validated hashes above. Rebuild from the handed-off source before promoting an artifact. Applying the archived patch later requires fresh source-bound validation and an actual offline run.

A separate cold/noinline reader-retry experiment was rejected: a small single-thread improvement accompanied worse four-thread results. It never changed runtime source. Earlier require-inlining, single-page shortcuts, grouped-continuation and noinline-reader-entry experiments also lacked consistent gains. Avoid repeating them without new evidence.

## Known risks and unfinished work, in priority order

1. **Performance and architecture review.** Separate game CPU time, memory guards, renderer submission and presentation costs. Run one bounded before/after experiment against an actual offline route, without concurrent builds/debuggers. Preserve correctness; do not treat disabling checks as a solution.
2. **Physical iPad acceptance.** Check current device/signing availability once. Earlier checks had neither a connected device nor development identity. Device packaging needs provisioning retaining Extended Virtual Addressing; the engine reserves a 4 GiB virtual range. This is not a claim of 4 GiB physical usage, nor proof it fits the device's memory budget. See [Development](DEVELOPMENT.md).
3. **Reproducible build/package.** Remove dependence on unexplained local preparation; inventory required generated inputs and document regeneration. Verify source-to-bundle identity before distributing. Source backup is not a portable app release.
4. **Stability and complete controls.** Investigate the isolated null guest-PC abort from an earlier eight-operation baseline (~404 seconds). Repeated and longer sessions did not reproduce it; it is not proven fixed. Prior report: `~/Library/Logs/DiagnosticReports/Retired/TribesPad-2026-10-09-172739.ips`, if still retained. Test real simultaneous touch, skiing, station navigation, controller, background/resume and long sessions.
5. **Mac and iPhone.** Rebuild/regress Mac shared-runtime changes and get hands-on acceptance. Implement and exercise full iPhone layout/device-family support, one Simulator at a time; do not present the diagnostic controls shell as the full game.
6. **Fidelity and product completion.** Compare representative terrain/interiors/water/weapons/players/vehicles, HD sampling and audio against the pinned reference. Resolve online test scope with the human only when ready; no automatic joining. Full original product acceptance remains outstanding.

## Build and workspace handoff

Workspace: `/Users/chrissotraidis/GitHub/tribespad`. **Do not delete it after pulling GitHub.** At wrap-up, ignored `ref/` was about 3.7 GB and `generated/` about 250 GB. Git contains source, input manifests and selected validation evidence, not the game assets, complete translated inputs, native dependencies or all logs. A fresh clone is not currently a one-command reproducible build. Do not commit those ignored directories wholesale.

Prepared baseline used by the full build: `generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe`. Preserve it until a clean reproduction succeeds. Local Python is `.venv/bin/python`; pinned LLVM was `/opt/homebrew/Cellar/llvm@20/20.1.8/bin`. Xcode SDK 26.5 was used; accidentally selecting the Command Line Tools SDK 27 previously caused TAPI incompatibilities.

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

Boot only one Simulator. Install/launch `com.tribespad.full-engine.ipad` on its current UDID; don't rely on old process IDs. Start **LAN GAME → HOST → Katabatic → two bots → START GAME**. After loading, enable **Touch controls** and Fire to spawn. Leave the match, then **Launch → Quit** to exercise normal cleanup. [README](../README.md) has the control layout. The Mac development app path is `generated/integration-review/macos-engine-app/TribesPad.app`; see [Development](DEVELOPMENT.md) for packaging and native input-helper commands.

Source/policy changes intentionally invalidate proof receipts. Refresh the specific affected tests, not every historical suite. For example, the interrupted context diagnostic required `test-sdl-io.py` and `test-guest-threads.py` after the reviewed-source identity changed; both passed normal/checked/ThreadSanitizer checks before the experiment build. Returning to validated source can invalidate those experimental receipts again. A build failure identifying stale proof is actionable dependency information, not permission to bypass it.

## Suggested first review session

1. Read this document, the current builder and three linked app evidence records. Inspect Git status and current artifact hashes. Do not repeat the complete historical audit.
2. Reproduce one original offline match from the handed-off code, recording binary identity and actual behavior. Establish native frame timing without debugger/screen-capture distortion; review the archived diagnostic before choosing it. Keep a separate visible capture as functional evidence.
3. Decide whether the next meaningful result is physical-device measurement or a specific architectural performance change. State the hypothesis, baseline, acceptance criterion and stop condition before coding. Avoid another sequence of isolated microbenchmarks with no playable improvement.
4. Report in plain language: what runs, measured responsiveness, what failed, and the next concrete decision. Update this handoff when evidence changes. Mark the project complete only after agreed platform/gameplay acceptance, not because all component tests pass.
