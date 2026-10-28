# TribesPad expansion: players, community clients, Vengeance and Ascend

Research date: 2026-10-28. Read-only public research and static binary inspection;
no communities were contacted and no game code was executed.

For the implementation sequence and same-app integration, start with the
[Vengeance and Ascend plan](VENGEANCE-ASCEND-PLAN.md). This document records the
supporting evidence and its limits.

## Decision

Keep **both Vengeance and Ascend** as explicit expansion candidates. For a
multiplayer-led product, investigate Ascend's community-client compatibility
alongside the shared runtime work now. Do not make Vengeance a prerequisite just
because it is older or has a smaller bootstrap executable.

The evidence does **not** establish that Ascend consistently has more players
than Vengeance. It does establish a substantial Ascend community toolchain,
recent server-hosting work and organized play. That is a good reason to target
Ascend, independently of an unproven population ranking. Vengeance has a
different strength: its campaign, plus community multiplayer and a historical
bot mod, give it value when no one else is online.

Follow-up distribution research recovered the publisher's original free-download
page and compared the actual packaged licenses with Tribes 2. It also verified
Vengeance master queries and two responding game servers. See the follow-up
sections below; the earlier lack of a usable Vengeance status source is resolved
for infrastructure checks, but not for population or gameplay acceptance.

The existing T1/T2 choices remain relevant to actual multiplayer. T1's community
publishes nightly play guidance; T2 has recorded recent organized matches. New
titles should connect to their existing communities rather than depend on a new
TribesPad-only player pool.

## Where people play and what they use

| Game | Evidence and practical entry point | Product consequence |
| --- | --- | --- |
| Starsiege: Tribes | [PlayT1](https://playt1.com/) and [tr1bes.us](https://tr1bes.us/) publish pubs around 8 PM US Eastern on Rosco's Mixtape, with Discord-organized pickups. | Surface the existing community and schedule for the already supported game. Published guidance is not a guarantee of occupancy every night. |
| Tribes 2 | [TribalOutpost match records](https://tribaloutpost.com/stats) include an October 10, 2026 session with 44 participants across five matches. Its server browser separates people from bots. | Existing community compatibility and bot play already have practical value. Session participants are not a simultaneous player count. |
| Vengeance | A [May 2026 firsthand discussion](https://www.reddit.com/r/Tribes/comments/1t4loz0/tribes_vengeance_games_regurarly_in_the_afternoon/) describes empty afternoons, organized nighttime play and EU-based Discord coordination. The [Revengeance package](https://tribesrevengeance.net/downloads/revengeance/) includes campaign content and community fixes. | Treat it as campaign plus organized multiplayer. Current player totals remain unknown. |
| Ascend | [Dodge's installation guide](https://www.dodgesdomain.com/docs/guide-install-setup/) uses the final client, Community login, TAMods and matching GOTY/OOTB mode; custom maps are installed through packages. A [June 26, 2026 pub-night announcement](https://www.reddit.com/r/Tribes/comments/1ufwf83/tribes_ascend_pub_night_626/) and [August Llamagrab relaunch](https://www.reddit.com/r/Tribes/comments/1w17c5v/llamagrab_is_back/) show organized play and ongoing infrastructure work. | The target is the community-compatible client, including mode selection and required maps. Starting an unmodified executable alone would not satisfy this use case. |

The June organizer described events at 9 PM Eastern on the last Friday of the
month. That is a dated schedule report, not confirmation of the next event.
Verify an announced event before displaying a countdown; sources often say
"EST" casually, so timezone/DST conversion must not be guessed.

Llamagrab's August operator announcement named Iowa, Frankfurt and Sydney
hosting regions. This establishes hosting availability at that announcement,
not an active player population in each region. Its website returned HTTP 403
to this research client, so current service behavior was not independently
validated. The announcement also listed then-current feature limitations; those
should not be presented as still current without rechecking.

## What the live Ascend services actually reported

Read the exact public endpoints used by
[Launcher V2](https://github.com/Dylan-B-D/ta-launcher/blob/ebc4f39/src-tauri/src/commands/data.rs):

- [Community status](http://ta.kfk4ever.com:9080/detailed_status)
- [PUG status](http://ta.dodgesdomain.com:9080/detailed_status)

| UTC on October 28 | Service | Logged-in clients | Clients in listed matches | Named server records |
| --- | --- | ---: | ---: | ---: |
| 03:16:44 | Community | 3 | 0 | 4 |
| 03:16:44 | PUG | 1 | 0 | 15 |
| 03:24:53 | Community | 2 | 0 | 4 |
| 03:24:53 | PUG | 1 | 0 | 15 |

This was approximately 11:17–11:25 PM Tuesday in US Eastern time, 4:17–4:25 AM
Wednesday in central Europe and 12:17–12:25 PM in Japan. These two nearby
observations are not peak-time sampling, a weekly census or evidence that the
game has no audience. Counts across services are not necessarily unique people.

The [login-server implementation](https://github.com/Griffon26/taserver/blob/1d44670/login_server/loginserver.py)
distinguishes login connections (`online_players_list`) from per-match players.
The PUG feed returned **362 server records**, but 347 had no name or mode. The
Community feed returned seven records, three unnamed. Do not display these as
362 and seven usable game servers. Even a named record is not independently
verified reachable. Launcher V2 also turns fetch failures into zero counts;
TribesPad should show **unavailable**, not **zero players**, on request failure.

[Vengeance's public stats page](https://stats.tribesrevengeance.net/servers)
lists 349 historical servers, with its newest listed record last seen four years
ago. It cannot supply a current comparison. The T2 JSON returned by web search
was dated October 24; the directly requested endpoint returned 403. Consequently
this report does not compare a live Ascend count with a cached T2 count.

Steam-only counts would also omit clients launched outside Steam. GitHub
activity, Discord membership and downloadable files are evidence of a community
or tools, not measurements of people currently playing.

Saved [anonymous snapshot evidence](validation/tribes-community-snapshot-2026-10-28.json).
No player names are included in that artifact.

## Follow-up: Vengeance infrastructure verified directly

On October 28, 2026 at **06:32:36 UTC**, the public
[OpenSpy feed for Vengeance](https://openspy-website.nyc3.digitaloceanspaces.com/servers/tribesv.json)
returned three named servers, all reporting zero players. Its HTTP Last-Modified
was 06:32:15 UTC. This is fresh status evidence, unlike the historical Revengeance
stats. The browser entry point is [OpenSpy's Vengeance list](https://openspy.net/server-list/tribesv).

The Engine.dll extracted from our Revengeance package contains service templates
`%s.master.tribesv.org`, `%s.ms%d.tribesv.org` and
`%s.available.tribesv.org`, plus profile/statistics endpoints under that domain.
The tested Vengeance master/availability/profile hostnames and `master.openspy.net`
resolved to the same address during this pass. DNS equality alone is insufficient,
so two actual GameSpy master-list requests were made at **06:36:05 UTC**:
`master.openspy.net:28910` and `tribesv.ms0.tribesv.org:28910`. Both successfully
returned the same three addresses for game `tribesv`.

Then one standard GameSpy2 details request was sent to each advertised address at
**06:37:06 UTC**. No authentication, player-roster request or gameplay join was made.

| Server | Direct result | Map / mode | Players |
| --- | --- | --- | ---: |
| `webehostin.com kansas v2 [NEW CONFiG]` | Valid response, about 153 ms; game version field 63446 | Spiridian / CTF | 0 / 16 |
| `=Team FoxTare= Europa` | Valid response, about 284 ms; game version field 63446 | Arid / Arena | 0 / 16 |
| Third master-listed server | No reply within five seconds | Unverified by direct query | Unknown |

This demonstrates working discovery through the package's service namespace and
two responding game servers. It does **not** establish that the sampled client
installs cleanly, joins either server, renders correctly or attracts players at a
given time. One UDP timeout is also not proof the third server is permanently down.
The observation was around 2:37 AM US Eastern / 7:37 AM central Europe.

The independent master-status site reported seven servers on October 27; today's
direct result is three. Neither number is a player count. Its suggestion that a
Vengeance-specific guide exists in OpenSpy's supported-games list was not borne
out: that downloaded list contained no Tribes entry. Prefer the actual package,
DNS, protocol and direct-server evidence over a generic status-page description.

The package's `!README FIRST!.txt` describes a separate Windows server browser
and historical key/compatibility troubleshooting. Its advice is older than the
current website's no-key claim. No key utility or installer was run. A builder
should validate the selected game files and implement the necessary connection
behavior, rather than blindly run every helper included in the repack.

[Follow-up evidence and source hashes](validation/tribes-distribution-followup-2026-10-28.json).

## Ascend's current community stack

1. **Game client:** Parting Gifts distribution linked by Dodge's guide. The
   archive is 4,974,702,145 bytes. Read its ZIP directory and extracted only 64
   Win32 executables/DLLs using validated HTTP ranges and ZIP CRC checks; about
   60.2 MB was transferred. No full game installation was performed.
2. **Community login/game services:** [taserver](https://github.com/Griffon26/taserver)
   replaces the login service and manages game-server integration. It is not a
   replacement rendering/physics engine. A TribesPad client should use existing
   compatible services; it does not need its own hosted community backend.
3. **TAMods:** the live package index advertises core version 1.2.4. The downloaded
   DLL is 32-bit x86, 2,290,688 bytes, with modern MSVC/UCRT dependencies and a
   static TLS directory. It supports much more than cosmetic changes: the
   [GOTY joining instructions](https://github.com/Griffon26/taserver/blob/1d44670/docs/user_manual/joining_goty_servers.md)
   require it for GOTY client behavior.
4. **Launchers/installers:** the established guide still links TribesLauncherSharp.
   [Launcher V2](https://github.com/Dylan-B-D/ta-launcher/releases/tag/0.1.4)
   has a March 8, 2026 release. [Wilderzone's TA Installer](https://github.com/wilderzone/ta-installer/tree/f134661)
   exposes a manifest/dependency-based install design and Windows/Linux paths.
   Its GitHub latest-release endpoint returned 404 during this pass; the website
   instead links CDN installers. Source activity alone does not prove adoption.
5. **Content:** community maps, shared asset packs, shaders, Ubermenu and routes.
   GOTY and OOTB must match the server; maps and assets must match as well.

A particularly useful verification: **all 64 extracted game binaries and the
separate TAMods DLL match the SHA-256 entries in the current
[Wilderzone content manifest](https://cdn.wilderzone.live/ta/game/manifest.json)**.
There were no mismatches among those 65 files. This connects the archived client
to the current packaging system. It does not prove every file in the full game,
successful gameplay, permission or source/binary reproducibility.

The manifest separates core, engine, textures, shaders, tutorial, individual maps
and optional mods/routes. Reuse that inventory as research input instead of
inventing a competing community content format. A native Apple recipe still
needs its own pinned, validated inputs and translated outputs. The source
installer's default `.org` CDN returned 403; the website-linked `.live` CDN
served the manifest successfully, so URLs need verification rather than blind
copying from source defaults.

## Native Apple feasibility: concrete Ascend results

The inspected client SHA-256 is
`51316f5726f12e97d2f6c25e514db5444b4fdeefd1e345a1f860cce0c850d1d8`.
It is a 17,321,984-byte i386 executable with a 12,810,889-byte `.text` section,
base `0x00400000`, relocation tables and no static PE TLS directory. Its file
version resource says `0, 0, 2787, 0`; this should not be treated as a proven
network/build compatibility identifier. All 65 inspected PE files are i386.

| Area | Binary/source evidence | Required work |
| --- | --- | --- |
| Graphics | Direct imports of D3D9, D3DCompiler_43, D3DX9_43 and D3DX11_43; delayed D3D11/D3D10/DXGI imports; embedded Shader Model 3 requirement | Adapt and extend the existing HaloPad D3D9/Metal reference, then validate Ascend's shader/resource requirements. Launcher defaults disable D3D10/11, but do not prove every newer API is unreachable. |
| Physics | PhysXExtensions/APEX imports; delayed PhysXLoader, with packaged PhysXCore and other modules | Map the actual CPU physics path, load behavior and callbacks. CUDA-related imports are present in middleware; this does not prove GPU PhysX is mandatory. |
| Audio | X3DAudio1_7/XAPOFX1_5 imports; XAudio2 configuration | Additional audio/COM compatibility. Vengeance's OpenAL reuse does not automatically solve Ascend audio. |
| Login/network | 271 imports from MctsInterface; its own Winsock, Windows crypto and delayed Steam APIs | Preserve community login, authentication and game-server joining. A server-list UI alone is insufficient. |
| Runtime | MSVC90 in the game; MSVC140/UCRT and static TLS in TAMods; overlapping middleware bases | More CRT/exception/TLS work and a consistent module address layout. |
| Community modifications | Public TAMods source detours ProcessEvent and patches UnrealScript native functions; SDK structures assume 32-bit game addresses | Integrate equivalent hooks into the AOT dispatch/guest ABI, or deliberately translate a matching mod build. Ordinary x86 injection cannot be copied unchanged into native ARM64. |

The direct static import closure reaches 11 packaged files, or 13 with delayed
imports. Neither count is a complete runtime set: PhysX/APEX and other modules
can load further DLLs dynamically. Conversely, bundled Awesomium debug files,
editor helpers and texture utilities are not all established gameplay needs.
Do not turn the 65-file inventory into a claim that all 65 must run.

Ran TribesPad's existing auditor and diagnostic SRW on the client and TAMods:

| Input | Audited `.text` instructions | Unsupported instruction sites | Additional lowering errors |
| --- | ---: | ---: | ---: |
| TribesAscend.exe | 3,478,979 | 4,369 | 2 |
| TAMods.dll | 519,075 | 91 | 29 |

Both completed diagnostic SRW output with exit 0, which **does not mean clean
translation**. The client auditor left 682,988 `.text` bytes unclassified; the
TAMods audit reported 23 overlapping decode findings. These require analysis.
Frequent gaps include unpack/vector operations, x87 comparisons that set CPU
flags and reciprocal-square-root instructions. Repeated sites are not separate
features or an effort estimate. No complete ARM64 link, game launch, login,
rendering, multiplayer or device performance result was obtained.

Unlike the sampled Vengeance client, this Ascend sample reached diagnostic
output without first stopping in an unusual executable section. That is a useful
positive finding, not proof that Ascend is easier overall.

[Detailed static audit and hashes](validation/ascend-static-audit-2026-10-28.json).
The [Vengeance investigation](VENGEANCE-FEASIBILITY.md) contains the earlier
five-module baseline and its separate blockers.

## What can be shared between the two ports

TribesPad's PE tooling, guest address space, CPU helpers, Windows wrappers,
threads, callbacks, sockets and Apple packaging are common foundations. Both
candidates can benefit from improved instruction coverage, DLL relocation,
exception handling and a D3D9 compatibility layer. The games still have different
engine binaries, physics, audio, UI and networking requirements.

There is more reuse available than the active TribesPad renderer alone suggests.
The already pinned **HaloPad/ProjectReach reference** at
`c4aad6873057e48cb7e6aca907797722ef7da5b9` contains substantial implementations:

| Reference code under `ref/halopad/` | Useful existing behavior | Limits for these ports |
| --- | --- | --- |
| `port/runtime/halopad_d3d9*.c`, `halopad_shader.c` and Metal host code | D3D9 COM devices, resources, render state, drawing, presentation and shader-bytecode-to-Metal translation | Halo-specific contracts; advertised vertex/pixel Shader Model 2, restricted render-target formats and no multisampling. Unsupported paths stop. |
| `port/runtime/halopad_seh.c` | Guest exception-chain dispatch through RaiseException and RtlUnwind, including callbacks to guest handlers | No translated access-violation delivery, nested/collided unwind support or arbitrary changed-context continuation. Integrate with TribesPad's CPU, stack and callback model before relying on it. |
| `port/runtime/halopad_dinput.c` | DirectInput8A keyboard, mouse and controller handling | Supports a specific interface and formats/properties. Check each game's actual interface requests and host-input wiring. |

These are source-level starting points, **not features already wired into
TribesPad or tested with either new game**. The dependency's graphics contract
and shader implementation explicitly target Halo's shader models 1/2. Ascend's
inspected executable contains UTF-16 `ps_3_0`, `vs_3_0` and the message
“This game requires at least Shader Model 3.0” (file offset `0x00df28f0`). This is
strong static evidence of a capability gap; the exact reachable shader set still
needs extraction/testing. Simply advertising a higher version would not implement
it. No D3DCompile/D3DXCompileShader or XAudio2Create implementation was found in
the inspected reference runtime.

Consequently, the next graphics investigation should compare each game's shaders
and render-target usage against this existing implementation, rather than begin
a new D3D9 layer from scratch. Existing SEH also narrows the exception work, but
does not establish compatibility with the games' different CRT generations.
[Third-party documentation](THIRD-PARTY.md) already identifies these helpers as
same-owner code; preserve the provenance and applicable licenses of any reused
components. No blanket third-party license grant is inferred.

TAMods is an especially important acceptance target. Its public
[hook implementation](https://github.com/mcoot/tamods/blob/9b2aac346e469aafdf9734dbe3a19f458ff67520/TAMods/Hooks.cpp)
uses an x86 detour. TribesPad's [dispatcher](../port/core/guest_dispatch.c) routes
original addresses into a finite table of compiled functions. Changing bytes in
guest memory does not automatically change already compiled ARM64 behavior.
An explicit, versioned hook/dispatch integration is needed. Compiling the mod's
C++ unchanged for ARM64 would also change pointer layout and fail to reproduce
the original guest object ABI.

The inspected public source identifies TAMods 1.2.0 while the package index says
1.2.4. Source-to-binary parity has not been established. Old TAMods web docs call
the core closed-source; a public MIT-licensed source repository now exists, but
that alone does not prove it reproduces the currently served binary.

## Solo play and bots

- **Vengeance:** campaign is the dependable product reason to proceed. The
  [TRIBESai author's page](https://www.moddb.com/mods/tribesai) documents a CTF
  mod with bots that defend, capture, repair and use vehicles. It requires
  specially prepared maps; it is not a switch enabling bots on every map. The
  project dates to 2006 and has not been tested against the sampled build here.
- **Ascend:** [roam-map and route replay](https://www.tamods.org/docs/developer/api/doc_dev_api_routes/)
  are real community features. The API's `enableBot` means replaying a recorded
  route with a bot; it is not a combat AI population for ordinary matches. No
  established general combat-bot solution was verified. Avoid promising offline
  bot matches or assuming a local practice map proves login-independent startup.

## Implementation priorities grounded in player value

1. **Define both supported experiences:** Vengeance campaign/community servers;
   Ascend community login with GOTY/OOTB, TAMods, required maps and route practice.
   These are two optional future game profiles, not currently available options.
2. **Do shared foundations with both binaries as fixtures:** instructions,
   module placement, CRT/exception contracts and adaptation of the existing
   HaloPad D3D9/SEH/DirectInput code. Vengeance UCC/Core remains
   a useful small bootstrap test, but it should not dictate product priority.
3. **Ascend-specific first proof:** establish the exact client/mod pairing and an
   AOT-compatible hook mechanism; then login, open a practice map, and join one
   existing community server. Test GOTY/OOTB interoperability before calling it
   community-compatible. Native server hosting is a separate project.
4. **Vengeance-specific first proof:** UCC/Core, headless map/physics, client
   bootstrap, then one campaign scene; subsequently verify a community match.
5. **Builder enablement:** expose either game only after its own acceptance
   milestones pass. An optional game failure must not damage the existing T1/T2
   build. Verify clean player builds, storage, physical-device performance and
   controller/touch play before release.

A shared game-discovery surface could be useful sooner: community links,
verified event times and honest server status. It must distinguish **supported
here**, **available on Windows** and **under investigation**; a link to a Windows
launcher must not appear to promise native iPhone/iPad gameplay.

Windows installation is a separate, nearer-term possibility: the original game
already runs on that target and community installers exist. Integrating those
installations would not establish native Apple support. The public installer is
MIT-licensed; taserver/LauncherSharp have AGPL terms; the game and proprietary
middleware retain separate rights. Community hosting, source availability and
free downloads do not establish permission to redistribute translated games.

## Follow-up: free downloads, repackaging and the Tribes 2 precedent

**Vengeance has a verified publisher free-download history.** It is not necessary
to infer permission to download from the fact that the game is no longer sold.
Recovered the [January 12, 2016 archive of Hi-Rez's Tribes Universe page](https://web.archive.org/web/20160112180455/http://www.tribesuniverse.com/).
It offers legacy game downloads, links Vengeance directly to
`http://lwcdn.hi-rezgame.net/media/iso/TribesVengeance.zip`, and points Tribes 2
users to TribesNEXT patches. It retains a copyright/all-rights-reserved footer.
The October 2015 release is also documented by
[contemporaneous reporting](https://www.shacknews.com/article/91979/you-can-download-the-entire-collection-of-tribes-games-for-free-right-now).
The inspected 2016 page links Ascend to its free-to-play website, rather than to
the later Parting Gifts ZIP.

The live publisher page and old `lwcdn.hi-rezgame.net` host did not resolve during
this pass. Therefore the historical publisher ZIP has not been compared byte for
byte with today's community Revengeance installer. These are separate claims:
the publisher offered Vengeance free; the community now serves an extended build;
that extended build has not thereby become an officially authorized native port.
No public-domain dedication or general redistribution grant was found.

Being off sale does not itself expire copyright. The
[US Copyright Office's explanation](https://copyright.gov/history/copyright-exhibit/lifecycle/)
ties expiration to the copyright term, not current commercial availability.
This is general US context, not a determination of all applicable jurisdictions.

### What is actually inside the distributions

Read the game EULAs themselves, not just the Vengeance development-kit license:

| Input inspected | Personal-use terms and retained restrictions | What it establishes |
| --- | --- | --- |
| Tribes 2 `ref/control/base/EULA.txt` | Section 1 grants use of a copy; 3A restricts translation/reverse engineering/modification; 3C restricts transfers and replacement matchmaking. Section 10 preserves narrowly conditioned essential-use adaptations and archival copies. | The existing T2 input is also proprietary freeware with retained restrictions. It is not a broadly redistributable engine. |
| Revengeance `Content/System/EULA.txt` | Original Sierra game license. Sections 1, 3A/3C and 12 have broadly comparable use, modification, transfer, matchmaking and essential-use/archival provisions. | Vengeance's legal starting point is closer to T2's than the earlier report suggested. Its bundled license does not grant general native-port distribution. |
| Parting Gifts `TribesGame/tribes_eula.txt` | Dated September 4, 2015. Personal noncommercial service use; restrictions on copying, modification, translation, unauthorized mods and replacement connections. | The actual community-distributed client retains proprietary terms; public hosting alone does not identify an additional permission. |
| [Publisher-hosted Ascend EULA PDF](https://webcdn.hirezstudios.com/hirez-studios/legal/EULA-Tribes.pdf) | Retrieved successfully on the follow-up, dated November 10, 2015. Sections 3–4 retain personal-use and modification/distribution limits, including an applicable-law qualification for reverse engineering. | Corroborates that free-to-play is not an open redistribution license. It is a different dated document from the packaged text, not assumed to supersede every user's terms. |

The [Hi-Rez legal FAQ](https://www.hirezstudios.com/legal) is additional context,
not the principal license for this analysis. Its fan-content section allows
limited noncommercial maps for older Tribes games and excludes Ascend. It does
not settle whether a particular interoperability implementation is lawful, or
whether a community project has separate permission.

These restrictions are not, by themselves, a complete legal analysis. For example,
[17 USC 117](https://www.law.cornell.edu/uscode/text/17/117) addresses certain
essential-use adaptations by owners of program copies, with separate limits on
transferring adaptations; [17 USC 1201(f)](https://www.law.cornell.edu/uscode/text/17/1201#f)
contains a conditioned interoperability provision. Whether a particular license,
copy, port, patch or distribution qualifies is fact- and jurisdiction-dependent.
Neither "all personal builds are allowed" nor "the EULA makes every port
impossible" follows from this evidence.

### What TribesPad already does, and what a new profile could do

The existing repository provides a useful engineering pattern:

1. [Input manifests](../inputs-manifest.json) pin upstream downloads by SHA-256.
   The T2 installer comes from the depot linked by TribesNEXT; the community patch
   is a separate input. Each input's rights field explicitly says redistribution
   authorization has not been established.
2. The player's machine extracts and translates those inputs; the repository
   supplies runtime/tool source and translation differences. Game files and full
   generated game code remain in ignored directories.
3. [The builder](../scripts/build-tribespad.sh) bundles the locally obtained assets
   into the player's app and packages it locally. The resulting IPA contains game
   material even though the published repository does not. Giving that IPA to
   other people is a distinct distribution action.

This resembles the historical separation described by TribesNEXT authors:
[Krash](https://www.tribesnext.com/forum/discussion/comment/28991) distinguishes
the publisher's freeware availability from permission to publish on Steam, and
[Thyth](https://www.tribesnext.com/forum/discussion/comment/29021) describes the
original freeware installer plus a separate interoperability patch. Thyth also
states that TribesNEXT's own code/patch is separately copyrighted. Their 2017
comments explain their project's model; they are not permission for TribesPad
or a legal ruling that automatically covers native recompilation.

For Vengeance and Ascend, the concrete proposal is therefore **an optional local
build/import profile following the current T2 architecture**, once the runtime
works and the input/distribution basis is settled:

| Builder step | Vengeance | Ascend |
| --- | --- | --- |
| Acquire | Accept an identified local installation/archive. Record publisher-release provenance separately from community-package provenance. Enable upstream download only for a selected, documented source. | Accept an identified final-client installation/archive; verify game and mod hashes separately. A working community CDN is evidence of availability, not automatic authority to mirror it. |
| Assemble locally | Extract game/campaign files and the selected community compatibility changes. Preserve licenses and notices. | Extract the client, selected maps/assets and exact TAMods pairing; configure the selected community service and mode. Preserve licenses and notices. |
| Publish from this project | Runtime, build recipe, manifests and appropriately reviewed compatibility work. | Same separation, respecting each open-source tool/mod's own license. |
| Output for the player | Locally generated Mac app/IPA after native-port validation. | Locally generated Mac app/IPA after native-port validation. |
| Not established by the evidence | Permission to mirror a modified full game or publish a ready-made native game download. | Permission to rehost the client/mod/content bundle or publish a ready-made native game download. |

User-supplied input is a workable implementation path when a download source or
its authorization is unresolved; it is not a claim that local adaptation has no
remaining legal questions. Likewise, a binary-diff format is a technical delivery
choice, not automatic proof that a patch contains no protected expression.
The next rights review should assess this specific recipe and its exact inputs
and outputs, rather than ask the vague question "is abandonware legal?" No
message to a rights holder or community maintainer has been sent.

## Local artifacts and limits

Game binaries and generated translation output remain ignored under
`ref/ascend-research/`, `generated/ascend-research/` and
`generated/analysis/ascend_*`. Downloaded source snapshots and raw status
responses are under `/private/tmp/tribespad-ascend-research-20261028/`; these are
research extracts, not extra project checkouts. No installers or package scripts
were run, no user credentials were used, and no accounts or posts were created.

The unresolved population question needs event-window observations across
several days, using a verified Vengeance query source and the relevant Ascend
services. This pass does not leave a background monitor or scheduled task running.
