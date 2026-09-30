# Tribes 2 — Online + HD Apple Client Target

Research date: 30 September 2026  
Status: Product target selected; Apple implementation remains unproven.  
Scope: Tribes 2 only. This is a target specification and investigation handoff, not a completed port or a declaration of release readiness.

## 1. Product decision

Build the actual Tribes 2 client for iPadOS, iOS and macOS, running locally as ARM64 code, joining the existing PC community and displaying upgraded textures. Prioritize iPad acceptance. Existing-community multiplayer and enhanced graphics are both release requirements, not optional future features.

Do not substitute Tribes 1, Aerial Assault, a Tribes-inspired remake, streaming, a mobile-only server population, or an offline bot demonstration. A stock-rendered/offline build may be an intermediate diagnostic result, but does not satisfy this product.

## 2. Selected reference configuration

| Layer | Selection | Qualification |
| --- | --- | --- |
| Original game | Tribes 2, build 25034 | Obtain the original installation from the official TribesNEXT distribution and verify the executable locally. [S1] |
| Community client | TribesNEXT QoL release line | Preferred online/visual reference; inspect and pin the actual installer and installed modules before choosing the translated image. [S2] |
| Initial server family | Existing Classic community servers | Target Legacy CTF+, DAD TRIBES and THE CUT; resolve current addresses and exact modes through the community browser. [S4] |
| Preferred graphics | PlayT2 HD Textures 2.0 (Reworked) | Candidate enhanced presentation, subject to archive, visual, compatibility and permission checks. [S3] |
| Alternate graphics | PlayT2 HD Textures 1.0 (Upscaled) | More directly aligned with an upscale of the original artwork; evaluate separately, not stacked blindly with 2.0. [S3] |
| Maps | A stock-map control and the chosen community server's actual rotation | Include current custom-map dependencies; do not assume the base installer covers the community rotation. [S5] |
| Runtime | Evaluate HaloPad/projectreach AOT machinery and Apple shell | Reuse candidates, not proof of Tribes compatibility. [S7, S8] |

The official QoL thread links `TribesNEXT_20250922_preview.exe`. Record that as a discovered candidate filename, not as proof of a September 2026 release or an immutable “latest” build. Its advertised Windows CPU/API dependencies still need a title-specific AOT audit. The developer describes larger-texture support, display improvements and compatibility with the older online ecosystem. [S2]

PlayT2 lists 1.0 as a waifu2x-processed upscale, approximately 248 MB, and 2.0 as reworked hybrid textures, approximately 214 MB. These are website download sizes, not measured installed sizes. No claim is made that all current players use either pack. [S3]

## 3. Define the reference before porting

Create two isolated desktop installations:

- **Product reference:** original game + a pinned official QoL installation + one selected HD pack + required maps. Reproduce a real existing-server session here first.
- **Diagnostic control:** clean original game, with a separately pinned older community patch only when needed to isolate a failure. This is not an alternate reduced-scope release.

Use a working desktop client as the behavioral comparison for account flow, server browsing, movement, weapon behavior, rendering and map loading. Record reference screenshots, a short gameplay capture and a reproducible test route.

The PlayT2 Config AIO is a convenient comparative installation: its site advertises bundled maps, scripts, interpolation settings and textures. Do not assume it contains the chosen QoL build or exactly the selected HD pack until its files have been inspected. [S9]

## 4. Freeze the actual inputs

Produce `inputs-manifest.json` with original URLs, acquisition time, SHA-256, installed paths, versions, module dependencies, author/rights notes and the chosen graphics profile. Keep executable images and texture packs separate. Never invent missing hashes or accept an unexpected executable silently.

Inventory imports, delayed/dynamic DLL loads, instruction requirements, initialization callbacks, executable-memory writes, patch hooks, graphics calls/extensions, sound and account dependencies. Compare actual behavior—not API-name counts—with the implemented HaloPad runtime.

Treat every translated image and required patch module explicitly. HaloPad currently documents unsupported third-party client DLL mods; do not assume its loader can accept arbitrary TribesNEXT modules without adaptation. [S7]

Tribes AOT work must use a separate project and a pinned HaloPad revision. Do not destabilize the active HaloPad tree or prematurely turn it into a universal Windows compatibility framework.

### First gate: the QoL patch is not an ordinary passive DLL

A later official support-thread page explains that the patch dynamically modifies/replaces engine code and uses fixed pointers into `Tribes2.exe` at guest image base `0x400000`. It also contains reports of QoL failures under CrossOver and Parallels on Apple M4; a later regular-patch success report is not a QoL success. These are published reports, not reproduced tests. [S11]

The initial audit must therefore identify `IFC22.dll` initialization, its full required module set, modified code ranges, pointer redirections and any generated trampolines. Preserve the expected **guest** address model; do not assume those addresses must be exposed as native Apple executable addresses. Map changed control-flow targets to precompiled ARM64 implementations. A write of x86 instructions into guest memory cannot be assumed to change previously compiled ARM64 behavior.

Use build-time transformations for deterministic patch operations where appropriate, with explicit handling for runtime data updates and callbacks. Prove the result against the original patched installation. Do not treat a single post-startup memory dump as automatic proof that every future patch operation has been captured.

The product requires the online-plus-HD experience, not every optional QoL feature. If wholesale patch translation is unsuitable, report whether authorized integration of the needed behavior into the AOT build is viable while retaining the same server and visual acceptance requirements. Do not silently fall back to an offline or low-resolution product.

## 5. Rendering and HD acceptance

The Apple implementation must reproduce Tribes' required rendering behavior rather than assume HaloPad's D3D9 renderer is interchangeable with it. Audit the selected installation's graphics boundary before choosing a Metal implementation or compatibility library. [S2, S7]

Proposed requirements:

- Keep the selected HD source textures through decoding, upload and sampling; detect silent downsampling.
- Preserve original map geometry, collision, fog/visibility rules, cloak effects, hit detection and game-state behavior.
- Validate texture names, replacement precedence, archive contents, transparency, mip levels and memory use. Separate cosmetic resources from scripts or executable patches.
- Use correct device aspect ratio and a legible HUD. Do not stretch the original framebuffer as the finished presentation.
- Compare stock and enhanced rendering at the same locations and viewpoints. Test representative terrain, interiors, water, weapons, players and vehicles; document whatever the pack does not replace.
- Target sustained 60 FPS on the first accepted iPad profile. This is a goal, not a forecast. Measure busy online scenes, frame-time spikes, peak memory and thermal behavior before choosing default resolution/quality.

An HD texture pack is not evidence of a new renderer, new models, or an independently playable “HD edition.” Do not market the target as an official remaster.

## 6. Existing-community online acceptance

Use a controlled dedicated server with consenting PC peers first, then a real public community server. Community web listings establish destinations, not a measured current human population or a successful Apple connection. Separate bots, spectators and humans in test records.

Preserve legitimate account creation/login and each required server's authentication flow. Do not disable authentication or alter the server to hide a broken client. Validate address discovery, UDP behavior, packet timing, version negotiation, map/resource loading and reconnection.

Test at least two independently configured compatible servers. Avoid hardcoding a single public IP as the product's network backend. If a server requires a particular authentication or mod variant, document that explicitly instead of claiming universal support.

For a local Classic server control, TacoServer documents Classic 1.5.2 as its foundation. That is a server-stack dependency, not proof that every player must install the full server mod. Pin the actual server revision used in testing. [S6]

## 7. Milestones and required evidence

1. **Desktop online + HD reference.** Login, find the chosen server, enter a match and confirm the selected graphics are loaded. Deliver file manifests and reference captures.
2. **ARM64 execution and rendering.** Reach the actual game's menus and a controllable stock map. Log unsupported execution paths; do not substitute success-returning stubs for missing critical behavior.
3. **Physical-iPad controlled multiplayer + HD.** Complete at least 30 minutes with a PC peer. Exercise skiing, jetting, weapons, damage/death/respawn, inventory, vehicles and a map transition. Record frame times and compare observed behavior.
4. **Existing-community match.** Join through the normal supported online flow, finish a match, rotate to a custom map, disconnect and reconnect with the HD pack still active.
5. **Reproducible personal build.** From documented inputs, create a signed no-JIT ARM64 app that cold-launches normally. Test touch, controller and keyboard/mouse separately; report platform-specific acceptance rather than extrapolating from Simulator or macOS.

Proof is incomplete until online and HD work together. A server browser, bot match, injected development scene or a good-looking screenshot alone is insufficient.

## 8. Presentation and permissions

Purely aesthetic customization is permitted under the published anniversary-league rule, while modifications providing an advantage are restricted. This is not specific approval of either texture pack or a new Apple client; obtain operator approval for competitive testing. [S10]

Verify authorization and component terms before incorporating community patch code or redistributing graphics. Keep proprietary inputs and generated game code out of a public source repository. Establish whether the initial deliverable must be a personal Mac-side build rather than a universal prebuilt app. Do not promise an App Store release or redistribution clearance.

## 9. What this research did and did not establish

Verified public documentation identifies the online ecosystem, named graphics packs, current Classic server destinations, organized matches and a relevant existing Apple AOT project. [S1–S9]

The analysis environment could not resolve the installer/download hosts. The HD archives and QoL binary were not downloaded, extracted, hashed, benchmarked or checked for licensing contents. Gallery image fetches also failed; no visual comparison was completed. No Tribes executable was translated, no Apple Tribes build ran and no server was joined.

Next executable task: reproduce and freeze the desktop **online + HD** reference, then generate the exact compatibility gap report against HaloPad. Do not relax the product target to bypass a failed test; identify the failing component and investigate it directly.

## Sources

- [S1 — TribesNEXT official game and patch distribution](https://www.tribesnext.com/)
- [S2 — Krash's official QoL preview announcement and technical notes](https://www.tribesnext.com/forum/discussion/4430/preview-qol-fixes-update)
- [S3 — PlayT2 graphics-pack catalog](https://playt2.com/textures)
- [S4 — PlayT2 community server list](https://playt2.com/servers)
- [S5 — 25th-anniversary actual match schedule and map pool](https://tribaloutpost.com/league/25th-anniversary/schedule)
- [S6 — TacoServer README and Classic dependency](https://github.com/ChocoTaco1/TacoServer/blob/Stable/README.md)
- [S7 — Current HaloPad README](https://github.com/chrissotraidis/projectreach/blob/main/README.md)
- [S8 — HaloPad detailed device-status log](https://github.com/chrissotraidis/projectreach/blob/main/docs/STATUS.md)
- [S9 — PlayT2 installation options](https://playt2.com/)
- [S10 — Anniversary league modification rules](https://tribaloutpost.com/league/25th-anniversary/rules)

- [S11 — Later QoL support discussion: fixed-address engine patching and Apple desktop reports](https://tribesnext.com/forum/discussion/comment/30697)
