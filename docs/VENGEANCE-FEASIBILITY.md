# Tribes: Vengeance feasibility investigation

Investigated 2026-10-28 against TribesPad commit
`1c046b431bd085c394e612f462fb075ec91caa30`.

The [29 October implementation plan](VENGEANCE-IMPLEMENTATION-PLAN.md) turns these
findings into the next Vengeance work sequence, including actual input bindings,
campaign targets and a candidate module layout. This document retains the
28 October audit baseline.

## Recommendation

For the subsequent player/community comparison and Ascend binary investigation,
see [Tribes expansion research](TRIBES-EXPANSION-RESEARCH.md). Both games remain
explicit candidates; the small Vengeance bootstrap is an engineering experiment,
not a requirement to finish Vengeance before investigating Ascend.

Vengeance is a worthwhile **additional engine port within TribesPad**, but is not
ready to become another builder option. Its campaign gives it value independent
of multiplayer population. TribesPad supplies a substantial host runtime and translation
toolchain; it does not yet supply Vengeance's renderer, complete DLL address
layout, C++ exception behavior, or instruction coverage.

The next useful experiment is an ARM64 **UCC + Core bootstrap**, followed by a
headless engine/map load. Do that before implementing a launcher card or a public
PadMint recipe. This investigation did not produce a runnable port.

## Distribution and executable evidence

Downloaded the community [Tribes Revengeance package](https://tribesrevengeance.net/downloads/revengeance/)
from its [download directory](https://downloads.tribesrevengeance.net/).
The archive is 3,093,197,450 bytes; SHA-256:
`22180a8162caccc059c81aac86ad7c3e26b854e01f080534eb774b0ce088fc52`.
It contains an Inno Setup 5.5.6 installer (`setup.exe` plus two data volumes).
Used 7-Zip and innoextract to extract Program/Bin and Content/System without
executing the installer or any Windows program. The installer listing includes
campaign maps and Havok data. Full campaign content was not extracted or tested.

All 49 inspected PE files identify as i386 (32-bit x86), including tools and
helpers that are not necessarily gameplay dependencies. No static PE TLS
directory was found in these files; dynamic TLS usage still needs support.

| Component | Observed role or dependency | Implication |
| --- | --- | --- |
| `TV_CD_DVD.exe` (2,142,208 bytes) | Client, version resource 1.0.1; imports Engine, Core, gameplay DLLs, DirectDraw and COM APIs | Client bootstrap itself needs investigation |
| `Core.dll` (815,104 bytes) | 2,680 exports; Windows and MSVC runtime dependencies | Good initial runtime target |
| `Engine.dll` (4,800,512 bytes) | 7,592 exports; Core, rendering/AI helpers, IFC23, Winsock, MSVC runtime | Most engine functionality is native binary code |
| `D3DDrv.dll` / `RTCShader.dll` | Both import `d3d9.dll!Direct3DCreate9` | Requires a Direct3D 9 implementation/bridge |
| `WinDrv.dll` | DirectInput 8, IMM32 and window APIs | Existing input facilities need new API adapters |
| `ALAudio.dll` | OpenAL DLL/function-name strings; imports Vorbisfile | Existing OpenAL provider is useful, subject to interface checks |
| `ogg.dll`, `vorbis.dll`, `vorbisfile.dll` | Bundled codec libraries | Lift them or deliberately substitute compatible native wrappers |
| `MSVCR71.dll` / `MSVCP71.dll` | Visual C++ 7.1 runtime | C/C++ ABI and exception support need a deliberate strategy |
| `UCC.exe` (102,400 bytes) | Imports only Core, MSVCR71 and Kernel32 directly | Smallest practical bootstrap experiment |

The client's transitive **static import closure** includes 20 packaged modules.
Adding the configured D3DDrv, ALAudio, WinDrv, IpDrv and TribesGui roots brings
that to 26 modules (12,943,360 file bytes). This is a lower bound: dynamic loads,
OpenAL, native Unreal packages, effects, Tyrion and script-selected classes can
add requirements. Conversely, bundled editors, .NET helpers and utilities are
not automatically requirements for gameplay. Default.ini also names editor
packages, so a minimal runtime set must be measured, not inferred from filenames.

Default.ini selects `D3DDrv.D3DRenderDevice`, `ALAudio.ALAudioSubsystem`,
`IpDrv.TcpNetDriver`, `TribesGui.TribesGUIController` and `WinDrv.WindowsClient`.
It contains a commented OpenGL device option, but **no OpenGLDrv.dll occurs in
the installer listing**. That comment is not an available OpenGL port.

## Observed blockers

### Graphics

TribesPad currently routes OpenGL through GL4ES and ANGLE to Metal. There is no
corresponding Direct3D 9 entry point/device implementation in the current port.
Vengeance needs device/COM ABI handling, resources, render states, shader
translation, presentation and guest memory conversion. Reusing the Metal-facing
host infrastructure helps, but does not turn D3D9 calls into OpenGL calls.

The subsequent cross-game pass found a substantial starting point in the
already pinned HaloPad reference (`ref/halopad`, revision
`c4aad6873057e48cb7e6aca907797722ef7da5b9`): its D3D9 device/resource/draw code
and shader-to-Metal implementation. It is not currently wired into TribesPad.
It targets Halo's shader models 1/2 and selected formats, and refuses unsupported
paths. Adapt that implementation and compare Vengeance's actual shaders and
render targets before estimating the remaining graphics work. A whole new D3D9
implementation should not be assumed necessary.

The executable also imports DirectDrawCreate and COM/OLE APIs. Their actual
startup/movie reachability is unmeasured. No Bink DLL was identified in this
package; it should not be assumed to be a required dependency.

### Module placement and Windows C++ behavior

Preferred image ranges overlap. For example, Core occupies
`0x10100000..0x10426000`, while Engine occupies
`0x10300000..0x10c22000` (end addresses exclusive). Multiple other DLLs prefer
`0x10000000`; IpDrv and the client also conflict with Engine's preferred range.
TribesPad's [module registry](../port/core/guest_modules.c) rejects overlapping
images in the same guest address space. Its loader initializes already mapped,
translated modules; it is not an arbitrary Windows PE loader.

A deterministic nonoverlapping layout must be applied before lifting, including
relocations, imports, export addresses and indirect-call dispatch. The modules
generally retain relocation tables, which makes this a tractable engineering
task. A raw concatenation of separately translated DLLs will not work.

Core/Engine and other modules import Microsoft exception machinery including
`__CxxFrameHandler`, `_CxxThrowException` and `_except_handler3`.
[guest_raise_exception.c](../port/core/guest_raise_exception.c) currently handles
only the narrowly specified thread-name notification and aborts other cases.
Guest unwinding, error paths and setjmp/longjmp semantics must be implemented or
shown unnecessary for each supported path. Import-name matches alone do not
establish API compatibility. Translating MSVCR71 does not remove its dependency
on Windows exception behavior.

The same HaloPad reference also contains `halopad_seh.c`, implementing guest
RaiseException/RtlUnwind dispatch, and a DirectInput8A implementation. These
reduce the amount of work starting from scratch, but need integration with
TribesPad's CPU/callback/stack and input contracts. The SEH implementation does
not support translated access violations, nested/collided unwinds or arbitrary
changed-context continuation. No Vengeance compatibility is established yet.

### Translation baseline

Ran the existing code auditor and existing SRW binary on five unmodified input
modules, with generated audit hints, `HALOPAD_SRW_DIAG=1` and
`TRIBES_SRW_FULL_FLAGS=1`. No Vengeance-specific fixes were applied.

| Module | Auditor instructions in `.text` | SRW result | Unsupported-instruction diagnostics |
| --- | ---: | --- | ---: |
| UCC | 18,748 | Diagnostic output, exit 0 | 18 |
| Core | 136,056 | Diagnostic output, exit 0 | 18 |
| Engine | 949,301 | Diagnostic output, exit 0 | 43 |
| D3DDrv | 96,852 | Diagnostic output, exit 0 | 2,533, plus 34 lowering errors |
| TV_CD_DVD | 28,227 | Aborted, exit 6 | Invalid instruction at `0x109be6d8` |

These are static diagnostic counts, not executed failures or independent
features. Repeated vector routines account for many sites. UCC/Core include
`prefetchnta` and `movntq`; Engine additionally exposes SSE gaps; D3DDrv contains
substantial SSE/MMX/3DNow code. Some CPU-specific alternatives might be avoidable
through a proven CPU-dispatch choice, but that has not been established. Do not
silently stub instructions that move or compute data.

**Diagnostic SRW exit 0 is not clean translation.** No LLVM link, ARM64 game
execution, renderer, controller, campaign, multiplayer or device test passed in
this investigation. The auditor's `.text` coverage is also not whole-client
coverage: the client has an additional executable `.kpjaig` section with high
entropy, and the SRW failure is inside that section. Unusual sections and
`SIntfNT.dll` strings are protection/packing indicators, not proof of which
protection is active. The bootstrap needs provenance and static/runtime study;
no protection was bypassed in this investigation.

## What TribesPad can reuse

| Existing component | Reuse assessment |
| --- | --- |
| PE auditing, SRW/llasm, LLVM and ARM64 build tools | Reuse the pipeline, add instruction support and module layout; do not reuse T2/T1 address-specific fixes |
| Guest memory, x87 helpers, threads, TLS, callbacks, clocks, heaps and filesystem | Strong foundation; audit each newly reached contract |
| Module/export registry and DLL initialization | Useful foundation after a rebasing/import-resolution plan |
| Native OpenAL provider | Promising direct reuse after checking Vengeance's requested functions, context behavior and extensions |
| Socket and SDL input facilities | Reuse host facilities; Vengeance-specific Win32/DirectInput/protocol behavior remains |
| Apple app shell, signing, packaging and touch/controller UI | Reuse after runtime bring-up; gameplay bindings need validation |
| Current OpenGL renderer | Host presentation infrastructure helps; it does not implement D3D9 |
| Pinned HaloPad reference D3D9/Metal, SEH and DirectInput8A | Substantial additional source to adapt; Halo-specific capabilities and guest ABI need validation, and these components are not wired into the active TribesPad runtime |

The [builder](../scripts/build-tribespad.sh) currently accepts only `tribes2` or
`tribes2,starsiege`, and builds Tribes 2 first. Adding an option would be simple
compared with the port. A real integration also needs pinned input hashes, Inno
extraction (a new build dependency or replacement extractor), content manifests,
per-game build/cache outputs, DLL registration, startup selection, configuration,
launcher handling, disk-space measurement and clean player-build validation.
Keep it opt-in until it passes those checks; don't make existing players build a
large experimental third runtime by default.

## Practical sequence and stopping points

1. **Input and bootstrap:** establish the supported binary set and distribution
   basis; create a fixed module layout; get UCC/Core to a command-line response on
   macOS ARM64 with no reached diagnostic stubs. This tests DLL calls, CRT, files,
   configuration and exception behavior without the client renderer.
2. **Engine without graphics:** initialize the remaining required native packages
   and load a known map with a null renderer if that path works in this build.
   Validate actual AI/physics/script behavior and missing imports. UCC working
   alone would not prove the protected client works.
3. **Playable slice:** resolve client bootstrap, adapt/extend the reference D3D9 surface needed
   by one map, and reach a campaign scene with movement, skiing, weapons, sound
   and save/load. Use campaign content to avoid dependence on populated servers.
4. **Apple devices and builder:** verify performance/memory, input and lifecycle
   on physical devices, then packaging and an opt-in recipe. Measure additional
   storage and clean build time rather than extrapolating from today's inputs.
5. **Network acceptance:** test LAN/direct connect, discovery, authentication and
   actual community-server compatibility. Existing Winsock wrappers do not prove
   multiplayer compatibility; no live player count or server join was checked here.

Until steps 1–2 succeed, a completion estimate would be speculative. This is
closer to another engine port than to adding another downloadable content pack.

## Source and distribution limitations

The public [TribesRevengeance-Source tree](https://github.com/jkelin/TribesRevengeance-Source)
contains UnrealScript/mod development material; it does not establish an
available, licensed, complete native engine replacement. Its
[developer-kit readme](https://github.com/jkelin/TribesRevengeance-Source/blob/master/Readme.txt)
includes restrictive license terms covering translation, modification and
redistribution. That text concerns the development kit; it is not a definitive
analysis of every game distribution or jurisdiction.

The follow-up recovered the publisher's original free-download page, inspected
the **actual game's EULA** inside the community package, and compared it with the
T2 EULA and TribesNEXT's separate-installer/patch model. Vengeance has verified
publisher free-download history; its proprietary license is broadly comparable
to the T2 starting point. It has not been shown to grant general distribution of
a modified native game. See [the detailed distribution comparison](TRIBES-EXPANSION-RESEARCH.md#follow-up-free-downloads-repackaging-and-the-tribes-2-precedent).

A future optional local build/import recipe could follow TribesPad's existing
architecture. Exact download provenance, personal-adaptation basis and the
contents distributed by the project still need their own assessment; free
availability is not equivalent to permission to ship a finished game IPA.

The later [live infrastructure check](TRIBES-EXPANSION-RESEARCH.md#follow-up-vengeance-infrastructure-verified-directly)
also resolved three servers from both OpenSpy and the package's `tribesv.org`
namespace, then received direct status responses from two. This improves the
evidence that community infrastructure works, but does not establish a client
install, gameplay join or player population.

## Evidence and reproducibility

[Sanitized audit metadata](validation/vengeance-static-audit-2026-10-28.json)
records module hashes, PE metadata, dependency closures and diagnostic counts.
It contains no game executable bytes or translated game code.

Local ignored research artifacts are under `ref/vengeance-research/` (download
and extracted inputs) and `generated/vengeance-research/` (inventory/probe scripts,
logs and generated outputs). Auditor intermediates are under
`generated/analysis/vengeance_*`. These paths are research artifacts, not a new
supported build command. The probe uses `.venv/bin/python`, `scripts/audit-code.py`
and `generated/tools/SRW-tribes/SRW.exe`; the SRW hash is recorded in the metadata.
