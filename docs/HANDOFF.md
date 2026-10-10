# TribesPad handoff — 10 October 2026

## Read this first

**The original game runs offline on Mac and in the iPad Simulator. It is not a finished, comfortably playable Apple port.** The primary engineering problem is runtime performance, followed by physical-device validation and reproducible packaging. This is not currently blocked on another missing startup API or on permission to use this Mac.

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

**Baseline (10 October, native timing):** offline Katabatic, two bots, untouched observer view in the iPad Air 11-inch (M4) Simulator runs at **6.0–7.0 FPS** (means 6.75 and 6.2 over clean 30-second windows in two runs of the same source); map load took 221 s. Swap submission averages 0.04 ms, so the game thread is CPU-bound, not presentation-bound. Evidence: [frame timing and guard review](validation/ipad-frame-timing-guard-review.json). The opt-in `TRIBES_FRAME_TIMING=1` logger in `gl4es_context.c` produced these numbers and is now part of the source; it measures CPU-side swap submission, not GPU completion. Run-to-run variation on this shared Mac is about 8%, so only large differences are meaningful.

**Where the time goes:** 71% of game-thread self time is the software memory guard (`tribes_memory_access_begin/end`, `require`, block-entry host-span checks, thread-local lookups). Translated game code is about 3% and x87/SoftFloat about 5%. The game performs roughly 12 million checked guest accesses per frame, and each out-of-line check costs about 8.5 ns (two calls, two thread-local lookups, bounds and permission tests). The reader announce/release atomics are not the cost: a sticky-reader variant was slower in isolation. Secondary hotspots: indirect-target verification binary-searches 523,444 registered targets per indirect call (~5%); the SDL/GL backend re-reads allocator callbacks and revalidates every public SDL buffer on every GL call, including immediate-mode vertices (~8%); Metal `waitUntilCompleted` under framebuffer binds (~4%).

**Rejected:** inlining permission checks for later accesses in existing 16-access groups cut `access_begin` samples by 26% and passed fault and ThreadSanitizer tests, but measured 6.0–6.3 FPS (no measurable change), grew the binary from 104 to 190 MB and the build from 12 to 44 minutes. Most remaining checks are single accesses inside small runtime helpers, which grouping cannot reach. Patch: [inline-grouped-guard-rejected.patch](experiments/inline-grouped-guard-rejected.patch). Do not repeat call-path micro-tuning: even a free software guard would cap this scene near 23 FPS, and an out-of-line call per access cannot get close to that.

**Shortest credible route:** let the MMU enforce the checked permissions. Every 16 KiB host page whose four 4 KiB guest pages agree gets native protections and needs no software check; a page with mixed 4 KiB permissions is mapped inaccessible, and its faults are decoded, checked exactly, then aborted or emulated through a private alias and resumed. A census of 29.4 billion checked accesses found mixed-page accesses only at startup (7,610) and 0.47 per million in gameplay (one partly committed heap page), with no gameplay writes to pages holding published code: about 40 emulated faults per second. A component spike ([fault_emulation_spike.c](../tests/fault_emulation_spike.c)) passes on macOS and in the iOS Simulator: native 0.5–0.9 ns accesses, about 2.1 µs per emulated access, and denied accesses abort before taking effect. Evidence: [hardware permission feasibility](validation/hardware-permission-feasibility.json). This keeps abort-on-violation semantics but changes the enforcement mechanism, so it needs an explicit design review before it replaces the brackets.

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

1. **Hardware-enforced checked memory.** Design review, then implementation: native page protections for uniform host pages, exact fault emulation for mixed pages (complete AArch64 load/store/atomic/SIMD decoding, signal-safe coordination with permission publication, published-code write protection, debugger behaviour), removal of per-access brackets, then the same offline route with native timing. Acceptance: at least 2x steady observer FPS with all permission and abort tests passing; stop and reassess below 1.5x. Smaller follow-ups afterwards: O(1) indirect-target verification and lighter per-GL-call SDL validation.
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

Boot only one Simulator. Install/launch `com.tribespad.full-engine.ipad` on its current UDID; don't rely on old process IDs. Start **LAN GAME → HOST → Katabatic → two bots → START GAME**. After loading, enable **Touch controls** and Fire to spawn. Leave the match, then **Launch → Quit** to exercise normal cleanup. [README](../README.md) has the control layout. The Mac development app path is `generated/integration-review/macos-engine-app/TribesPad.app`; see [Development](DEVELOPMENT.md) for packaging and native input-helper commands.

Source/policy changes intentionally invalidate proof receipts. Refresh the specific affected tests, not every historical suite. For example, the interrupted context diagnostic required `test-sdl-io.py` and `test-guest-threads.py` after the reviewed-source identity changed; both passed normal/checked/ThreadSanitizer checks before the experiment build. Returning to validated source can invalidate those experimental receipts again. A build failure identifying stale proof is actionable dependency information, not permission to bypass it.

## Suggested next session

1. Read this document and the two new validation records. Inspect Git status and confirm the Simulator binary hash before relying on it.
2. Review the hardware-enforcement design (risk 1) with the user; it changes how checks are enforced, not what they enforce.
3. If approved, extend the spike into the runtime behind an explicit build option, keep the bracketed build as the reference, and measure both on the same route with `TRIBES_FRAME_TIMING=1`.
4. Report what runs, measured frame rate, what failed and the next decision. Do not claim completion from component tests or rendering alone.
