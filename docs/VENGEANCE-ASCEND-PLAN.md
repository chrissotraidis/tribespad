# Vengeance and Ascend in TribesPad

Implementation handoff, 28 October 2026. Source inspection baseline:
`1c046b431bd085c394e612f462fb075ec91caa30`. **Planning and research only: neither
game currently builds or plays in TribesPad.** Recheck the implementation before
starting; existing plans and validation records describe different checkpoints.

## Product decision and scope

Add **Tribes: Vengeance** and **Tribes: Ascend** as optional engines and game-data
sets in the **same TribesPad app**, beside Starsiege: Tribes and Tribes 2. Keep the
existing app identity, installation, picker, signing and personal-build model.
One engine runs per app launch. This is not a proposal to run these games inside
the Tribes 2 engine, ship Windows launchers as Apple games, or merge their worlds.

The owner requested this plan and authorized proceeding with the community ZIPs
as the likely implementation inputs. Use them as the starting point; do not stall
engineering research by repeatedly asking whether to investigate those same
files. Verify exact versions, hashes and extraction results before relying on
them. The publisher's Vengeance free-download history is established. The
community repack is still a distinct input, and this direction is not a claim
that every possible redistribution is licensed.

The intended experiences are:

- **Vengeance:** campaign, save/load, movement/skiing/jetpack, weapons and audio;
  then joining the existing community multiplayer service. Investigate the old
  TRIBESai CTF mod only after the base game works; campaign AI does not establish
  general multiplayer bots.
- **Ascend:** the final client with community login, compatible TAMods hooks,
  GOTY/OOTB selection, practice maps and joining existing community servers with
  matching maps/assets. A menu without community compatibility is not completion.

Neither game is a prerequisite for investigating the other. Vengeance's smaller
UCC/Core bootstrap is a useful shared-runtime test. Ascend's community experience
is a product priority in its own right. Existing T1/T2 support must remain usable.

## Start here: evidence that already exists

| Evidence | Use it for |
| --- | --- |
| [Expansion research](TRIBES-EXPANSION-RESEARCH.md) | Community workflows, live observations, Ascend dependencies, reuse and distribution comparison |
| [Vengeance feasibility](VENGEANCE-FEASIBILITY.md) | Installer contents, five-module translation baseline, client bootstrap and DLL placement problems |
| [Vengeance audit](validation/vengeance-static-audit-2026-10-28.json) | File identities, import inventory and diagnostic measurements |
| [Ascend audit](validation/ascend-static-audit-2026-10-28.json) | Client/mod identities, imports, translation gaps and HaloPad reference identity |
| [Distribution/server follow-up](validation/tribes-distribution-followup-2026-10-28.json) | Actual packaged license identities, publisher archive, successful master queries and two direct game-server responses |
| [Community snapshots](validation/tribes-community-snapshot-2026-10-28.json) | Timestamped, anonymous Ascend service observations; not a population ranking |

Do not repeat the entire research pass before making progress. Missing data that
matters to the next executable milestone is worth investigating. GitHub commits,
Discord membership and download availability are not player counts. The October
28 Vengeance checks establish working discovery and responding game servers,
not successful client gameplay; the two Ascend observations do not establish
typical concurrency. Live observations will need refreshing for online acceptance.

## Candidate inputs and acquisition

| Input | Candidate source | Known identity / remaining work |
| --- | --- | --- |
| Vengeance community game | [Revengeance download page](https://tribesrevengeance.net/downloads/revengeance/), [ZIP](https://downloads.tribesrevengeance.net/TribesVengeance.zip) | 3,093,197,450 bytes; whole-archive SHA-256 `22180a8162caccc059c81aac86ad7c3e26b854e01f080534eb774b0ce088fc52`. Contains Inno Setup 5.5.6 installer plus data volumes. |
| Ascend final client | [Community setup guide](https://www.dodgesdomain.com/docs/guide-install-setup/), [Parting Gifts ZIP](https://f000.backblazeb2.com/file/tribes-zip/Tribes_Ascend_Parting_Gifts.zip) | 4,974,702,145 bytes. Only selected files were downloaded by ranges; **whole-archive SHA-256 is not yet known**. Client SHA-256 `51316f5726f12e97d2f6c25e514db5444b4fdeefd1e345a1f860cce0c850d1d8`. |
| Ascend community mod | [TAMods package](https://client.update.tamods.org/packages/tamods-dll.zip) | ZIP SHA-256 `402853ebc900d7e3ff21c7e2c853bc94e2a669ee73430b15164536345cd384a7`; DLL SHA-256 `ad89f604aef91be88e9b5103f6133f8f2ff328191fc99a5c83099054ba5a353a`. Served package index says 1.2.4; inspected public source says 1.2.0. Establish parity before choosing source-built versus translated hooks. |
| Ascend modular content inventory | [Wilderzone manifest](https://cdn.wilderzone.live/ta/game/manifest.json), [installer source](https://github.com/wilderzone/ta-installer) | All 64 inspected game binaries plus TAMods matched this manifest. Full content was not validated. Pin a manifest snapshot and selected packages; never silently follow a moving latest manifest. |

Start with the recorded community builds, not a new repack assembled from unrelated
mirrors. Support a local archive/installation override for research and download
failures, with the same hash requirements. Source URLs are candidate upstreams,
not permission to mirror the files in this repository. Preserve supplied notices.

The input-preparation implementation should:

1. Fetch/resume into a temporary file, verify size and whole-file SHA-256, then
   atomically promote to cache. A changed hash stops with an input-version error.
2. Extract into an isolated staging directory; reject escaping paths and symlinks.
   Vengeance needs 7-Zip plus a pinned/recorded Inno extractor. Do not execute the
   installer or every helper/key utility included in the archive.
3. Inventory module and asset identities. Separate shipping game dependencies from
   editors, development kits, debug middleware and optional content. The 49/65 PE
   inventories are not required runtime-module lists.
4. Retain the original layout where Unreal expects it, including Vengeance's
   `Program/Bin` and `Content` and Ascend's `Binaries`, `Engine` and `TribesGame`.
   Record allowed writable paths separately from immutable input files.
5. Record game, community patch/mod and optional map versions independently. A
   readable JSON manifest is enough; no generic package-manager framework is needed.

## Fit into the existing app

The present code is the authority for integration, not the older launcher plan:

| Existing seam | Planned extension |
| --- | --- |
| [Build command](../scripts/build-tribespad.sh) accepts T2 or T2+Starsiege | Eventually accept opt-in `vengeance` and `ascend` game IDs. Keep current defaults. Initially retain T2 in the aggregate build; game-specific diagnostic scripts can run independently. |
| [Mac picker](../port/apple/launcher_mac.m) execs a bundled engine | Add `VengeanceEngine` and `AscendEngine` beside existing engines in the same app bundle after each is ready. Preserve the one-process-at-a-time model. |
| [iOS entry](../port/apple/mobile_engine_main.m) selects before engine startup | Add one entry point per new engine; allocate/init only the selected engine's guest space. No second UIKit application or SDL main loop. |
| [Starsiege embedding](../scripts/starsiege/build_engine.py) localizes symbols with `ld -r` | Use the same pattern: only `vengeance_mobile_engine_main` or `ascend_mobile_engine_main` exported from each relocatable engine object. Audit undefined symbols and intentionally shared libraries. |
| [App packager](../scripts/build-ipad-engine.py), [Starsiege bundler](../scripts/starsiege/bundle_mac.py) | Add per-game manifests/data only for successfully built selections; keep the existing T2 profile intact. Mac helper executables and all frameworks must be included in signing checks. |
| [Launcher enum](../port/apple/launcher.h), UIKit/AppKit card implementations | Add stable game IDs and a small shared descriptor table where useful. Replace two-card index assumptions; keep the current visual language, using a responsive grid/list for up to four cards. |
| [Mobile controls](../port/apple/mobile_controls.m) contains T1/T2-specific branches | Introduce explicit game capabilities/bindings rather than assuming every non-Starsiege game is T2. Reuse overlay mechanics, not T2 script commands or registry keys. |
| [PadMint recipe](../padmint.json), [manual build workflow](../.github/workflows/build-ipa.yml) | Update only after working profiles and measured requirements exist. The recipe currently hardcodes both old games; do not claim game-selection UI exists in PadMint. |

The iOS object-localization pattern isolates C symbols, not every kind of process
state. Check Objective-C class names, signal handlers, thread-local CPU pointers,
global callbacks, SDL ownership and shared renderer/audio libraries. A successful
link does not prove engines are isolated. Keep all guest DLLs of one game inside
that engine's guest space; never register them in another game's live state.

Switching games should follow the current lifecycle: release input, save/flush
the selected game's state, close, and show the picker at the next launch. The
current iOS Choose Game action closes the app and requires the user to reopen it;
do not promise an in-process engine swap or automatic iOS restart. Retain the Mac
picker/exec approach. A missing/stale selection returns to a working picker.

Use `GameData/Vengeance` and `GameData/Ascend` inside the bundle and separate
`Application Support/TribesPad/Vengeance` and `/Ascend` writable roots. Keep T2/T1
paths compatible with existing installs. Settings, saves, profiles, downloads and
logs must not overwrite another game or be reset on a data-version update.
Avoid copying every immutable multi-GB asset into writable storage if the guest
filesystem mapping can serve it from the bundle; measure whichever mapping works.

Compiled engine presence and validated data determine playability. A downloaded
ZIP alone must not create a Play button. No runtime x86 JIT, arbitrary DLL loading
or downloading executable code is part of this native-AOT plan. Assets imported
after signing must match an engine already built into that app.

## Runtime work shared by both games

**Module layout comes before a trustworthy lift.** Many DLL preferred addresses
overlap. Produce a deterministic per-game address map; apply PE relocations and
resolve imports/exports before generating audit hints, indirect-call targets and
translated objects. The current [module registry](../port/core/guest_modules.c)
initializes registered AOT images; it is not a general Windows loader. The
[auditor](../scripts/audit-code.py) preserves some fixed-address literals. Do not
rebase its existing outputs afterward and assume those literals moved too.
Regenerate all derived inputs and prove cross-DLL calls, pointers, ordinals and
callbacks against the chosen layout. Account for TAMods' fixed SDK addresses.

Reuse the pinned HaloPad/ProjectReach reference
`c4aad6873057e48cb7e6aca907797722ef7da5b9` already in
[dependencies.lock.json](../dependencies.lock.json):

- `ref/halopad/port/runtime/halopad_d3d9*.c`, `halopad_shader.c` and its Metal host
  code provide a D3D9 starting point. The advertised contract is Shader Model 2,
  selected render-target formats and no multisampling. Ascend contains an SM3
  requirement. Extract each game's shaders and render-target needs, measure gaps,
  then implement them; changing capability flags alone is not an implementation.
- `halopad_seh.c` provides guest exception-chain dispatch and RtlUnwind. Adapt its
  CPU/stack/callback contracts; it does not cover all faults, nested/collided
  unwinds or arbitrary context restoration. Test reached C++ exception paths for
  each CRT generation, plus DLL startup/teardown and thread-local initialization.
- `halopad_dinput.c` provides DirectInput8A keyboard/mouse/controller behavior.
  Compare actual interface, data-format and property requests. Share host input
  devices deliberately; do not duplicate controller events through two backends.
- TribesPad supplies guest memory checks, CPU helpers, threads, files, sockets,
  OpenAL, app lifecycle and packaging. Matching an import name is not API coverage.

Retain fail-fast diagnostics for unsupported behavior. Diagnostic SRW exit 0 is
not a playable build: it can emit traps. Instruction counts reflect sites, not
independent features. Add focused semantic checks when changing instruction
lowering or runtime contracts; avoid weakening checks to get past a bootstrap.
Extract shared code only when an actual second caller needs it.

## Milestones and completion evidence

Each row should become a small, reviewable implementation change with a compact
record under `docs/validation/`. Keep binary dumps and generated code ignored.
Record input hashes, tool/runtime commit, command, first failing guest address,
elapsed time and the precise behavior verified. Do not mark later rows complete
because an earlier build succeeded.

| Stage | Work | Completion evidence |
| --- | --- | --- |
| P0 — repeatable inputs | Add per-game acquisition/inventory scripts and manifests; obtain Ascend full-archive hash; capture dependency graphs and initial module maps. | Fresh staging extraction reproduces selected file hashes; corrupt or wrong-version input is rejected; no dependence on this machine's research caches. |
| P1-V — small Vengeance execution | Rebase selected UCC/Core dependencies; resolve reached translation/CRT/SEH/import contracts. Investigate the client `.kpjaig` decode stop separately. | Native ARM64 UCC/Core reaches an identified command-line response without a reached trap. Save exact command/output; this is not client startup. |
| P1-A — Ascend executable and mod path | Resolve audited code/relocation ambiguities and instruction gaps; choose a matching TAMods source/translation strategy and explicit AOT hook mechanism. | Representative game-to-middleware calls and guest ProcessEvent/native-function hooks execute correctly, including callbacks and TLS. Retain a precise remaining-startup-blocker list. |
| P2-V — engine/map and graphics | Headless map/AI/physics where available, then client bootstrap; adapt D3D9/Metal, OpenAL and input. | A real campaign scene renders with movement, skiing, weapons and audio; save/reload works. UCC alone does not satisfy this stage. |
| P2-A — actual client/practice | Bring up selected D3D9/SM3, shader compiler/resource paths, CPU PhysX/APEX and XAudio-related APIs. Integrate community authentication and practice-map flow. | The client reaches a playable practice map with movement, weapons and audio. Record whether practice requires login; do not infer offline independence. |
| P3 — existing community matches | Vengeance discovery/direct join through the selected package endpoints; Ascend login, GOTY/OOTB, TAMods and map compatibility. | Successful join and a sustained playable session against an existing service; disconnect/reconnect and content mismatch handling checked. An empty browser or successful UDP query is insufficient. |
| P4 — same-app integration | Bundle whichever new engine has passed its playable milestones; extend picker, paths, controls and lifecycle. | A combined app can launch each included game on separate launches; T1/T2 still work; saves survive upgrades and game changes. Test all four together once both new engines are ready. |
| P5 — physical devices/player build | Measure iPhone/iPad performance, memory, heat, load time, build time and peak disk; test controller/touch and lifecycle; verify clean builder/recipe. | Device gameplay evidence and clean-player-build results, with limitations stated. Only then describe the new option as playable or add it to a release recipe. |

Vengeance's client bootstrap currently stops in an unusual executable section;
the static data does not establish whether a protection path is active. Diagnose
the selected community binary before substituting files or claiming an engine
bootstrap solves the client. Use a compatible Windows reference for behavioral
comparison when available; never silently replace gameplay/physics with stubs.

Ascend's current audit reports 4,369 unsupported instruction sites in the client;
TAMods adds 91 sites and 29 lowering errors. Client unclassified code and TAMods
overlapping decodes also need analysis. PhysX/APEX modules may load dynamically;
CUDA imports do not prove GPU physics is mandatory. The game imports MSVC90,
while TAMods brings newer CRTs and static TLS. Neither OpenAL nor a D3D9 bridge
alone solves those separate requirements.

The [TAMods hook source](https://github.com/mcoot/tamods/blob/9b2aac346e469aafdf9734dbe3a19f458ff67520/TAMods/Hooks.cpp)
patches x86 execution. [TribesPad dispatch](../port/core/guest_dispatch.c) selects
precompiled code by guest address, so changing guest bytes does not change native
behavior. Integrate versioned hooks/compiled variants and preserve 32-bit guest
object layout. Compiling the mod unchanged for ARM64 is not a solution. Resolve
source/binary version parity before assuming the MIT repository reproduces 1.2.4.

## Build and acceptance details

Suggested future aggregate selections, **not commands supported today**:

```text
tribes2,starsiege                     existing default
tribes2,starsiege,vengeance           opt-in Vengeance
tribes2,starsiege,ascend              opt-in Ascend
tribes2,starsiege,vengeance,ascend     combined four-game app
```

An explicitly requested new game must not silently disappear behind a successful
build result. Preserve existing outputs and give a nonzero result identifying
the failed requested game; permit a deliberate retry without it. Do not redesign
Starsiege's existing fallback as a side effect of the first port change.

Keep per-game `config/<game>/`, `scripts/<game>/`, `port/<game>/`, ignored
`ref/<game>/` and `generated/<game>/<stage-or-platform>/` folders. Follow the
existing Starsiege arrangement without copying its hard-coded addresses. Cache
keys need input, tool/runtime, layout, hook/patch, platform and configuration
identities. A manifest change invalidates affected derived results.

The regression matrix includes T2-only, the current T1+T2 default, each single
new-game addition and the all-four build when available. Check unknown/duplicate
game IDs, wrong input hashes, missing data, unavailable selected games, build
resume, fresh install and upgrade with existing saves. Verify renderer selection
does not initialize another engine's graphics or reserve its guest memory.

For actual play, check campaign save/load (Vengeance), community authentication
and modes (Ascend), skiing/jetpack/weapons, menu text input, simultaneous touch
actions, controller reconnect, mouse/keyboard, audio interruptions, background/
resume and clean quit. Define a repeatable map/session and report measured frame
times and memory on named devices. No performance target or completion date is
justified by the static audit yet.

The current 20 GiB free-space allowance was measured for T1/T2. Do not carry it
over to four games: compressed inputs alone add about 8.1 GB before extracted
assets, lifted code, object files, staging and IPA duplication. Measure peak usage
and adjust per-selection requirements. Retaining one shared SDL/Metal library
does not mean each engine's compiled code or assets are small.

Keep `padmint.json`'s `publication.public_binaries: false` model. The existing
manual GitHub workflow uploads a game-containing IPA as a temporary artifact;
public-fork artifacts can be downloaded by others. Extending that workflow is a
separate distribution decision, not automatically covered by a local recipe.
Document the exact sources/outputs and remaining rights questions; do not label
the community ZIPs or a native game as MIT simply because some tools are MIT.

Before announcing a future release, follow the repository owner's PadMint rule:
run `scripts/player-check.sh` from PadMint's then-current `main` in
`/Users/chrissotraidis/GitHub/padforge`, with the clean game checkout before tagging
and without it after publishing. Use the script's current interface. Apple's
Python 3.9 and an empty PadMint work folder are required; warm-cache/Homebrew
builds do not substitute. A failure blocks the release. This documentation PR is
not a game release and does not require an unchanged multi-hour game rebuild.

## Next bot's first work session

1. Read this plan and the relevant audit JSON; inspect current `main` and local
   changes. Start from the existing checkout and use the owner's isolation rules.
2. Implement **P0** for both candidates: reproducible fetch/import, extraction,
   hashes and named dependency inventories. Acquire the missing full Ascend ZIP
   hash. Commit only scripts/manifests and sanitized evidence, not game files.
3. Produce a concrete nonoverlapping Vengeance UCC/Core module layout and begin
   **P1-V**. Also investigate Ascend's exact TAMods pairing and the first hook
   contract for **P1-A**, without waiting for Vengeance completion. Choose the
   next task by observed blockers.
4. Leave a small PR and a dated status entry stating the first reached failure,
   what passed, and the next exact experiment. Do not add public Play cards or
   recipe promises just because the engine object links.

This checkout currently has useful ignored inputs under `ref/vengeance-research/`
and `ref/ascend-research/`, and diagnostic scripts/results under
`generated/vengeance-research/`, `generated/ascend-research/` and
`generated/analysis/`. They are conveniences, **not part of the merged handoff**.
Research source extracts and raw service responses in `/private/tmp/` may vanish.
Use the committed URLs, hashes, audit metadata and existing `scripts/audit-code.py`
to recreate the experiments. The diagnostic baseline ran the auditor first,
copied its `relocations.csv` and `srw/` hints beside a uniquely named image, then
ran `generated/tools/SRW-tribes/SRW.exe IMAGE OUTPUT.llasm` in that directory with
`HALOPAD_SRW_DIAG=1 TRIBES_SRW_FULL_FLAGS=1`. Keep per-game/module image stems
distinct because the auditor keys output directories by stem. That procedure is
a static baseline, not a build or a gameplay test.
