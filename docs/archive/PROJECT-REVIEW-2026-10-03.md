# Project review and revised execution strategy — 3 October 2026

## Finding

TribesPad has a substantial ARM64 execution and Windows-service foundation, but it is not yet an integrated game application. Progress through initialization is real. It must not be confused with progress through the renderer, playable controls or authenticated multiplayer: those systems have not reached acceptance.

The most consequential architectural defect found in this review was SDL memory ownership. The bridge installed guest callback allocators as native SDL's global allocator, copied every opaque native allocation into guest memory, and required a synchronous owner CPU frame. That can pass startup and display tests while failing as soon as an audio worker allocates. A real CoreAudio playback experiment reproduced exactly that failure. This review therefore changes implementation direction at the native-library boundary instead of merely permitting another initialization flag.

## Evidence reviewed

- The user-selected Online + HD PRD, repository README, execution plan and current/historical status.
- Full startup harness, build/link scripts, LLVM memory/external passes, finite patch routing, thread ownership, SDL backend and allocator code, Apple entry point and diagnostic application.
- The pinned SDL3.2.18 CoreAudio and pthread implementations, especially native audio-device threads, asynchronous notifications, recursive mutexes and condition waits.
- The pinned ProjectReach reference. Its reusable engine execution and Apple infrastructure are useful; its DirectSound/D3D/Metal implementation does not establish compatibility with Tribes' SDL/OpenGL boundary.
- The latest accepted display checkpoint: all259 frozen files passed SHA verification; its integrated source hashes matched the workspace before this review's changes.
- The integrated import report and original PE analyses. Before this change452 imports remained unbound. This is a dependency census, not452 equally important next tasks. The canonical IFC22 translation still has553 explicit diagnostic traps.
- Real native experiments: the old bridge completed CoreAudio init/quit but aborted opening playback with `invalid curl allocator ownership/frame`. With private native ownership, the same test opened a real CoreAudio device, queued a silent stream and closed it. These are actual native calls, not fabricated successful results.

The existing checkpoint remains frozen under `generated/integration-review/display-immediates-checkpoint-20261003`. New source must earn its own acceptance; old passing results are not silently assigned to changed code.

## What exists and what remains unproven

| Area | Existing evidence | Remaining product gap |
| --- | --- | --- |
| Inputs | Pinned original build25034, selected QoL image, HD archive and isolated Apple profile | No working desktop online+HD reference session; no rendered proof that the selected pack is used |
| Original execution | Base-engine translation and real IFC22 process attach/engine entry;18 validated startup patch revisions |553 diagnostic sites remain in IFC22; later execution and game loops are unproven |
| Windows services | Real filesystem, memory, threading, timing, crypto and other reached services with checked ABI tests | Required later services, asynchronous I/O and complete lifecycle still need integration |
| Native libraries | Actual curl/TLS, SDL hints/events/video/display, public ABI conversion | SDL audio/input/window/GL/IO consumers are incomplete; curl's blocking outer lease can serialize unrelated transfers |
| Apple application | Objective-C controls, two real translated routine checks, server queries | `build-apple.py` links the two-routine diagnostic, not the complete engine |
| Complete engine host | Checked macOS startup harness executes original code | Startup and diagnostics live in `tests/qol_native_bootstrap.c`; no shared full-game UIKit runtime entry yet |
| Rendering | Original graphics import/string analyses | No renderer or original frame; no verified window, terrain, interiors, text or HD texture upload |
| Input | Touch/controller/keyboard/mouse actions in the diagnostic UI | Actions are not yet proven to drive original menu/game bindings; feel cannot be accepted outside gameplay |
| Networking | Native server-list/query behavior and separate real HTTP/TLS components | No legitimate account/login flow or authenticated connection to a match; no PC-peer test |
| Platforms | macOS execution; many real components in one iPad simulator | No full-game iPad/iPhone or physical-device acceptance |
| Reproducibility | Extensive source-bound reports and frozen artifacts | Source was still untracked except the original PRD at review start; cached SRW tool acquisition and a clean-machine full build remain open |

Graphics deserve particular caution. The absence of `gl*` names from the unbound import list is not evidence that graphics are implemented. The original engine analysis contains dynamic GetProcAddress names for GL/GLU. The executable has417 graphics-name strings; IFC22 has1758, including library capabilities that may never be used. Neither number is a runtime requirements list. Actual resolved functions, formats, extensions and original rendered scenes must drive the implementation.

## Architecture decision implemented in this review

Native SDL owns its opaque native allocations. A native audio worker must not borrow another thread's game CPU, guest stack or TLS. Internal64-bit objects are not32-bit public ABI structures and are no longer copied wholesale into the guest heap.

Guest-facing behavior remains explicit:

- `SDL_malloc/calloc/realloc/free` continue through the registered guest allocator bridge.
- Borrowed public results—hints and display modes—use bounded guest storage and checked copies, with native private pointers excluded. Cached modes retain their tested guest layout and identity checks.
- Guest allocators own the public allocations and are called for their cleanup. Native internal allocation counts, sizes and callback calls are intentionally different from the old mirror implementation. This is a representation change, not byte-for-byte allocation parity with Windows SDL.
- Native log messages emitted outside a guest frame are copied into a locked queue and delivered on the next owner entry. They retain category, priority, message and callback identity. Native workers do not execute guest logging code. Delivery timing/thread therefore differs for asynchronous logs; synchronous owner callbacks retain immediate dispatch and nested-entry checks.
- Native synchronization objects are exposed as typed, nonreused guest tokens. Lock/condition operations can run from real guest threads without holding an outer SDL owner lease. Native pointers remain private; wrong types/bases, destroyed handles, unlocked waits and destruction while in use stop explicitly. Recursive locks are supported; condition waits require one held level.

This direction removes the observed audio failure and also supplies the ownership model needed by native windows, events and audio streams. It does not automatically implement their public guest APIs. For example, successful native silent playback is not yet sound produced by the game's original mixing path.

## Revised work order

1. **Complete the reached multimedia path as a group.** Validate native ownership, logging, synchronization, audio lifecycle and then the original audio device/stream consumers. Include failure/cleanup paths and actual original execution. Keep the guest boundary checked. Do not accept silent-success audio stubs.
2. **Make one full-game startup entry shared by the app and diagnostics.** Extract lifecycle/configuration from the macOS-only harness without losing original image/TLS/thread/patch setup. Compile the full runtime and images for the sole iPad simulator and enter through SDL_RunApp. Keep diagnostic termination/reporting separate from application lifecycle. Do not present the two-routine shell as this milestone.
3. **Reach an original window and menu.** Implement the actual SDL window/property/event boundary and finite GL function dispatch. A native macOS GL path may provide an intermediate visual comparison, but does not establish an iOS backend. Select the mobile rendering implementation from observed fixed-function, extension and texture requirements. No no-op graphics acceptance.
4. **Load a real map and connect controls.** Drive original action bindings, test skiing/jetting/aim/fire together, then inventory, death/respawn and vehicles. Use real screenshots and repeated gameplay routes to refine touch geometry, sensitivity, cancellation and controller behavior.
5. **Prove HD and authenticated multiplayer together.** Verify archive replacement precedence, actual uploaded texture dimensions and visuals. Exercise legitimate login, controlled PC-peer play, public compatible servers, custom-map rotation and reconnect. A ping reply is not a connection.
6. **Accept each platform separately.** Continue one simulator at a time. After full iPad execution, test iPhone layout, macOS input and then signed physical iPad gameplay, frame times, memory and thermals. No extrapolation from component or simulator results.

The current missing import remains useful diagnostic evidence, but it is no longer the sole planning horizon. For each subsystem, inspect the adjacent actual consumers before rebuilding the whole client. The visible milestone is the original menu, followed by a controllable original map.

## Changes to the working loop

- Preserve validated instruction, patch and memory work; avoid speculative universal Windows compatibility development.
- Run relevant component tests once for the final sources, then one integrated original launch. Rebuild/retest when a change or observed failure requires it; do not repeat unrelated large suites or freeze the same evidence as a substitute for product progress.
- Batch related native APIs and original patch segments where their contracts are known. Keep unobserved paths labeled as unobserved.
- Keep a compact current status and this forward plan; historical detail stays available but does not override newer outcomes.
- Separate four kinds of evidence in every report: source/static inspection, component execution, original full-engine execution, and user-visible gameplay.
- Retain failing runs. An exit from a diagnostic harness, successful compiler invocation, instruction count or simulator control screen is not game startup success.

## Remaining high-impact risks

The renderer is still a major unimplemented subsystem. Full-engine UIKit hosting is a separate integration task. Missing guest APIs and diagnostic instructions can still stop later paths. Audio stream, IO and callback data need explicit32/64-bit marshalling. Authentication has not been exercised. There is no accepted desktop behavior reference or physical-device session. The no-JIT, original-game, HD and existing-community requirements remain intact.

The work is not complete, and there is no justified completion percentage or delivery date yet. `STATUS.md` records the latest integrated boundary and the results earned after this review.

## Verified outcome — 4 October 2026

The final full build now passes original AUDIO initialization. Inspection of its actual return gives EAX=1 and native `coreaudio`; ordinary execution reaches the subsequent `SDL_SetTLS` import. This required an explicit Apple mapping for the client's hardcoded `directsound` hint. The intermediate build that merely advanced to TLS actually returned zero from audio initialization, and its failing binary/log/CPU capture was preserved. Advancing to a later import alone was insufficient evidence.

Mac normal/ThreadSanitizer/checked and sole-iPad checked component runs pass real silent playback and deferred worker logging. Mutex/condition component tests also pass. Guest SDL TLS, audio stream marshalling and original mixing remain unfinished. Current evidence and next boundary are recorded in `STATUS.md`.

## Subsequent integration result — 4 October 2026

SDL TLS now uses real native per-thread storage, checked opaque guest IDs and cdecl destructor dispatch inside live-owner cleanup scopes. Mac normal/ThreadSanitizer/checked and sole-iPad checked TLS tests pass. Actual guest-thread finalizers run before stack/TEB release across return, ExitThread and FreeLibraryAndExitThread paths; the25-thread lifecycle regression passes on Mac and iPad. TLS mutation during native cleanup remains an explicit boundary.

The full original run now returns1 from SDL_SetTLS and obtains a native mutex token. It then reaches a new original startup rewrite at0x410c79; the checked code-write guard aborts as intended, and the integrated driver reports failure. The surrounding routine contains two related conditional redirects, so the next finite-patch work should handle that sequence together. No19th revision is enabled and no gameplay acceptance has been added. See the current status for binary hash and actual capture locations.
