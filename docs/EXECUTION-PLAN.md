# TribesPad execution plan

**Current iPad app:** checked ARM64 Simulator `891288e6…`, PID79871, renders offline Katabatic with two bots through ANGLE Metal. Touch spawn, movement, aiming, weapon switching, disc fire (15→14), jet elevation/drain/recovery and death/respawn pass. World first observed 485 seconds after Start Game. The distant terrain gap remains. Walking into an inventory station left the weapon unavailable until death/respawn; station exit needs investigation. [Offline evidence](validation/ipad-host-local-offline.json).

**Latest optimization:** `--cpu-host-checks` now proves constant in-bounds accesses to native stack, TLS and private globals, removing 53,316 redundant checks in addition to the existing register-prefix optimization. Guest pointees, uncertain/crossing ranges, volatile/atomic accesses and transfers stay checked. Boundary/fault/thread regressions, retirement grouping, 94,128 original SIMD oracle cases and mobile startup regressions pass. ThreadSanitizer in the new SIMD replay covers the native guard runtime, not the precompiled math. [Components](validation/ipad-host-local-components.json).

**Still too slow:** 14 recorded frames over 10.55 seconds during touch aim in an interior, with no compiler/debugger active. The prior build recorded ten over 9.253 seconds in a different view; this is not a controlled speedup comparison. Native sampling still concentrates in access checks and reader locks (252 access-begin, 159 rdlock, 123 unlock, 96 access-end top-of-stack samples). Do not use the original FPS counter. Terrain gaps and input latency remain unfinished.

**Build:** Simulator executable is 103.1 MB, versus 104.5 MB previously and 158.6 MB before CPU-prefix checks; strict signature verification passes. [Simulator receipt](validation/ipad-host-local-simulator-build.json). Build status now replaces the previous success receipt before compilation and only reports success after signature verification. Device checked math rebuilt (228 objects, zero failures); the matching device app also completed and passed strict ad-hoc signature verification. [Device receipt](validation/ipad-host-local-device-build.json). Both builds are finished.

**Next:** target native SIMD load/store/scalar-memory helpers: their inclusive barrier paths account for 64/63/48 samples. Investigate a bounded native CPU-object proof at helper entry while preserving checks on every guest operand; retain permission and concurrent-protection semantics. Do not rebuild or expand tests without a concrete change. The earlier forced SSE-inlining probe produced only a modest component gain and is not integrated. No physical iPad or development identity is available; device execution remains unverified. Offline matches/discovery only, no online join/authentication. Mac bundles unchanged.

## Earlier checkpoints

**New iPad milestone:** full original PID3943 renders offline Katabatic with two bots and accepts native touch controls. Fire spawns the player; Move changes position, Look rotates, Weapon switches to disc, Fire reduces ammo15→13, and Jet raises the view/drains energy followed by recovery after release. The primitive-restart boundary is resolved. [World](validation/ipad-katabatic-2026-10-08.png), [touch gameplay](validation/ipad-touch-gameplay-2026-10-08.png), [evidence](validation/ipad-offline-gameplay.json). Startup is slow, input has visible delay and some interior views show black triangular gaps; this is not finished playability or rendering-fidelity acceptance.

**Current work:** self-contained Simulator and ARM64 device builds complete. Prior bundled Simulator PID14125 rendered the original menus from its private Application Support profile. Native onscreen keys enter “Tribespad”; the focused field stays above the keyboard and Return dismisses it. [Keyboard](validation/ipad-keyboard-2026-10-08.png), [bundled execution](validation/ipad-bundled-startup.json), [builds](validation/ipad-bundled-build.json). Storage tests pass first install/preservation/loader refresh/failure recovery. Device ad-hoc signature verifies, but no physical device or development signing identity is available; installation and device execution remain unverified. The previous gameplay PID3943 was ended after preserving evidence. Next: reduce measured checked-memory hot-path cost and diagnose rendering gaps, then repeat the same offline touch match with the bundled app. Only offline matches and discovery; no online joining/authentication. Earlier pending-map statements below are superseded.

**Scope:** full iPadOS integration is the current user priority. Offline tests and server discovery only; no online joining or authentication. Accepted Mac bundles are retained. No physical iPad has run this app.

**Checked integration:** all 33 finite revisions remain enforced with the mobile 18-hook renderer layout and seven relocated audio hooks. The mobile graphics suite passes GUI shader/perspective/native-fence checks and 8/16/32-bit primitive-restart real-pixel checks, with 33 expected failures. Native touch component tests cover simultaneous actions, aggregated look, quick tap edges and cancellation. [Graphics](validation/ipad-gl4es-guest.json), [touch checks](validation/ipad-touch-controls.json), [app storage](validation/ipad-app-storage.json).

**Build optimization:** changed guard policies still rerun the checker; freshly checked byte-identical bitcode can reuse its hash-verified optimized ARM64 object. Latest cached Simulator and device builds each reused 407/409 objects. Both platforms now have their own compiled original modules; device builds bundle assets incrementally. This shortens iteration without disabling checked memory or claiming an FPS improvement.

The target is the original Tribes2 build25034 with the selected TribesNEXT client and HD textures, running locally as ARM64 on iPad, iPhone and Mac and joining existing community players. A working diagnostic shell or an offline map is an intermediate result. See the product target and current status for exact qualifications.

## Immediate integration work

**Current order:** full iPad startup/menu → rendered offline map → responsive touch/controller input → sustained performance/HD → physical iPad packaging. Preserve the accepted Mac app while optimizing source; remeasure Mac offline performance before promoting a new bundle. Server discovery is allowed; online joining is not.

The following are historical integration checkpoints; current mobile status and next action are above.

The full original Mac engine now publishes **33 finite revisions**, including the renderer's 26-hook commit after61 original stores and27 flushes. Its actual return, final detours, callbacks, bank and continuation bytes are verified. The exact `EB16` offcut at `0x6b0c21` is compiled and differentially checked. The original GarageGames splash now renders; see STATUS for the latest signed-app evidence.

Original Miles startup now returns **1** at `0x100555cc`. All 11 original plugins initialize successfully: four ASI codecs, six M3D providers and Reverb3.flt. The new M3D/filter group reuses existing checked AOT/loader infrastructure and adds 20,065 finite entries with eight explicit diagnostic stops. Normalized aliases work. Plugin preparation now rejects reserved/duplicate import namespaces after the first full run exposed and the corrected run resolved a collision with native system lookup.

The native DirectSound device, primary format and reached secondary PCM ring are implemented. Normal/checked/ThreadSanitizer tests observe real CoreAudio postmix samples, volume/pan, cursor movement/wrap/stop, finite completion and producer/callback lifetime. The full original engine and signed Mac app now create a 22050Hz stereo16 buffer (now 69120 bytes after correcting the emulated-driver flag) and begin looping playback. This is original startup progress, not audible game or playable acceptance.

USER timers, unnamed events, the bounded original thread-naming SEH continuation, native worker priority, event/mutex wait-any and checked Win32 byte strings are implemented and component-tested. Actual original execution verifies the thread-name handler and priority application. Original timer delivery and multiwait remain unobserved.

The real primary 3D listener now resolves the failed-reacquire/released-device crash. Full original execution next stops at SetSpeakerConfig(4), while the signed app's concurrent Miles worker reaches PSRAD at0x2114e23b. Listener normal/checked/TSan and actual full-engine/app evidence are saved in dsound-listener. Secondary positional DirectSound audio remains unimplemented.

**Current Mac milestone:** original internet discovery displays 26 live servers and native Refresh List succeeds. The curl reset boundary is fixed and tested in the full checked app; see STATUS and `validation/macos-curl-reset-server-list.json`. Next resolve Mac window focus/console punctuation and quit lifecycle, verify offline handoff on the updated binary, then user Mac assessment. No online match joining or mobile integration before that assessment.

Finite glGenerateMipmap0xfffa05a0 passes normal/checked/ThreadSanitizer tests with actual native mip levels/pixels and retained errors in core and legacy GL contexts;70 existing checked guards remain enforced. Changed source compiles for ARM64 device/Simulator without mobile execution. Restricted MAX_LEVEL has an independently reproduced native Apple discrepancy outside the captured default-range loader; no workaround is added. Source/probe/test/mobile receipts: `explicit-mipmap/`. All app/debugger/build processes ended, audio57 restored, no simulator booted. No rendered player model, map, HD or authenticated match acceptance. Account choice was asked once and remains pending; continue independent original offline-map/render/control work.

CancelIoEx exact/process-wide and CancelIo calling-thread selection are implemented over the existing pending socket registry. They mark requests; real workers/retained queues deliver actual status and partial counts. Normal/checked/ThreadSanitizer tests pass protected opaque identities, process/thread selection,32 ready/cancel races, ConnectEx and backpressured FIFO cancellation with exactly-one completion and sockets remaining open. All79 native/checked objects rebuilt, changed source compiled for ARM64 device/Simulator. Thread-exit cancellation and non-socket contracts remain explicit gates. Source/test/mobile receipts: `socket-cancel/`. All app/debugger/build processes ended; audio57 restored, no simulator booted. Account-choice question remains pending; continue independent original offline-map/render/control work while it is unanswered.

Zero-read readiness without consuming data, synchronous scatter reads, would-block/EOF and ordered asynchronous/synchronous sends are implemented through private native payload staging. Normal/checked/ThreadSanitizer tests exercise actual1MiB backpressure/multi-buffer FIFO, native byte transfer, close/cleanup/port-first cancellation and three memory faults. ConnectEx regressions pass. All79 native/checked objects rebuilt; changed source compiles for ARM64 device/Simulator, without mobile execution. Evidence: `tcp-io/{result.json,execution/,mobile-compile/result.json}`. Nonzero overlapped receive, callbacks/events and blocking paths remain explicit gates; thread-exit cancellation remains unimplemented.

ConnectEx/discovery/context update pass normal/checked/ThreadSanitizer tests: actual loopback connection and byte transfer, refused connection, deterministic pending/cancel/close/cleanup/port-close,32 native close races, emitted finite ABI and three memory faults. Existing queue/socket/Winsock regressions pass. All79 normal/checked objects rebuilt; changed socket/queue sources compile for ARM64 device/Simulator, without mobile execution. The original account flow now establishes a real connection, but successful application-protocol exchange/authentication remains unproven. Exact source/test/execution evidence: `connect-ex/{result.json,execution/}`, `connect-ex-mobile-compile/result.json`.

Socket/port association, retained keys and adjacent notification-mode component evidence remain valid. Signed debugger PID60478 confirmed the present original path skips SetFileCompletionNotificationModes. ConnectEx's reached main-thread caller stays alive; initiating-thread-exit cancellation remains unimplemented, as do initial-send/event/borrowed/blocking ConnectEx variants.

The finite glIsSync query fixes the original Quit PC0; normal/checked/ThreadSanitizer graphics checks pass70 intended faults. Signed PID53183 Escape Quit now reaches original ExitProcess(0), import175, after graphics cleanup. Implement process-wide termination with appropriate original DLL detach/lifecycle behavior; no return or invented success. Changed network/graphics sources compile for ARM64 device and Simulator, without mobile execution. Evidence: `handle-sync/execution/`, `wide-dns/execution/`.

The app still renders real login and supports actual EULA/account keyboard focus. Account registration was not submitted; a single account-choice question remains pending. No main menu/map, HD, authenticated match or full mobile acceptance. All builds/apps/debuggers ended, audio57 restored, no simulator booted. Continue independent map/control/rendering work when authentication input is needed.

The default Jump Desktop Audio device currently hangs in CoreAudio device opening. For controlled native/app testing, temporarily select MacBook Air Speakers using the existing generated openal-provider/audio-output helper and restore original default57 in a finally block. Real output succeeds with that selection; no null backend or absent-device success is substituted. This is a host testing workaround, not a shipped device-selection preference. See STATUS and openal-provider evidence.

Desktop permissions are resolved. Use `scripts/build-macos-engine-app.py` and launch the signed app for actual window/log verification; the latest signed app captures show the original TribesNEXT login UI. Both full-engine and audio component link scripts select the Xcode SDK explicitly. Two conditional audio-pointer writes at `0x7dc948` and `0x78e430` remain skipped after real VirtualProtect failures; implement bounded publication if required.

The prior encoding/private-heap/module-alias/FRNDINT validations remain valid. Original startup is now beyond those boundaries. Actual audio output, decoding, final plugin unmap/reload, menu and gameplay remain unaccepted. No mobile execution is claimed for the new plugin batch.

Mobile ES2 lacks COPY_WRITE_BUFFER and full-engine UIKit remains unfinished. Component and startup progress do not establish gameplay acceptance. See STATUS for evidence and exact scope.

## Product milestones

| Order | Deliverable | Acceptance evidence |
| --- | --- | --- |
|1|Original game window and menu|Actual SDL window/context/event APIs and dynamic GL calls render the original UI on Mac; observed output, no no-op graphics path|
|2|Original map and controls|Terrain/interiors/players/weapons render; original action bindings drive movement, skiing, jetting, aiming, firing, inventory, respawn and vehicles; original audio device/stream/mixer output verified when gameplay reaches it|
|3|Shared full-engine Apple startup|Complete original engine and QoL image enter through a reusable host on Mac and the sole iPad simulator; test harness and app share lifecycle and graphics contracts|
|4|Mobile gameplay and controls|Actual map and original actions work with touch/controller input on iPad, then iPhone, one simulator at a time; layout and lifecycle exercised in game|
|5|HD and authenticated community play|Actual HD texture uploads and visual replacement verified; legitimate account flow, controlled PC peer, public compatible match, required maps, reconnect and rotation exercised|
|6|Each Apple platform accepted|iPad then iPhone simulator, one at a time; Mac input/window lifecycle; signed physical iPad/iPhone frame times, memory, thermals and network interruptions|

## Integration rules

Preserve the 33 finite startup patch revisions and checked memory/callback boundary. No 34th revision or arbitrary executable-memory generation is admitted without the next bounded implementation and validation. Existing original instruction work remains useful, but a missing-import count or instruction count is not a completion metric.

Inspect adjacent actual consumers before rebuilding. Run relevant component tests for the changed source, then a complete original startup. Read actual API results as well as the next stop: the first audio-enabled build advanced to TLS even though initialization failed. Record static analysis, component execution, original execution and gameplay evidence separately.

Retain source-bound passing and failing evidence without repeatedly copying whole checkpoints. The accepted ownership checkpoint is `generated/integration-review/sdl-native-ownership-checkpoint-20261004`; the preceding display checkpoint is unchanged. Historical execution notes remain in `EXECUTION-HISTORY.md`.

## Unresolved dependencies

There is no accepted desktop online+HD comparison session, no Tribes renderer, no full-engine UIKit app, no authenticated match and no physical-device gameplay evidence. The clean-checkout SRW acquisition path also remains incomplete. These gaps must be closed rather than inferred from the extensive component work.
