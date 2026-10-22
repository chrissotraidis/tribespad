# Starsiege: Tribes in TribesPad: evidence and gated plan

Revised 2026-10-22 (research run 01:20–02:00 UTC). This replaces the first draft. Every source cited here was loaded during this run, and every number came from a command run during it. Nothing was built, no `.exe` was run, and no game was joined or logged into. Downloads and extracted files are in `/tmp/tribespad-research-t1/`, outside the repository.

The owner wants Starsiege: Tribes (Tribes 1, 1998) as a second game, chosen from a launcher with Starsiege: Tribes on the left and Tribes 2 on the right.

## Summary

The 2004 freeware installer is still downloadable from a stable archive, and its bytes match the hashes the archive publishes. Its sister Tribes 2 file in the same archive item has the same SHA-1 as the input TribesPad already pins, which ties the item to the FilePlanet freeware release TribesPad already depends on. Inside it is the retail 1.8 game plus the official 1.8 → 1.11 patch. The 1.8 `Tribes.exe` is a clean, unpacked 32-bit PE built with Borland C++. Its interface to the system is narrower than Tribes 2's in every way that matters: no Miles, no DirectInput, no DirectDraw, no SDL, one DirectSound entry point, and OpenGL loaded by name. The Borland compiler is the one new kind of input for the lifter. The Tribes 2 modules show no Borland markers. A lift preview on 22 October put the whole 1.8 executable through TribesPad's pinned toolchain to a native ARM64 object: 10 of 640,112 instructions stayed untranslated, all of kinds TribesPad already handles, and every runtime helper the object needs already exists. Translation is no longer the main risk; the Windows layer and the 1.11 patch are.

Three findings change the draft's plan:

1. **The 1.11 executable is a patch, not a file (now solved).** The official patch carries `Tribes.exe` as a 442,730-byte binary delta. TribesPad's pinned Wise extractor (REWise 0.3.1) lists patch support as unimplemented, so producing 1.11 needs either a Wise-patch decoder or a reference Windows/CrossOver install that applies it.
2. **The community 1.40/1.41 client is not part of the freeware release.** A community wiki says it "was leaked from GarageGames" and later repackaged into configs. That puts the client most players use on a different rights footing from the 2004 freeware. The first target should be freeware 1.11, offline.
3. **The rights holder's own download link does not contain Tribes 1.** Hi-Rez Studios' Tribes Universe page (archived March 2023) linked `StarsiegeAndStarsiegeTribes.iso` under its "Starsiege: Tribes" entry. The file is still live, but it is the 1999 Starsiege (mech game) CD. It contains no `Tribes.exe`.

The live population is small. On a US Wednesday evening (21:21 and 21:52 EDT), the masters listed 30 servers. Six and then four had players, 13 and then 9 people, and most were on RPG mods. The plain base game had 4 and then 2 (details in "Live population").

## How to read this document

**Verified** means a source loaded during this run says it, or a command run during this run measured it. **Estimate** means judgement from verified facts that nobody has tested yet. **Unknown** means nobody has checked it, and a gate below exists to settle it. A source that could not be loaded is listed as not loaded. Its claims are downgraded, even when they are probably true.

## Evidence table

| # | Claim | Status | Evidence (loaded 2026-10-22) |
|---|---|---|---|
| 1 | Starsiege: Tribes was re-released free in 2004 to promote Tribes: Vengeance | Verified (community sources) | PCGamingWiki page text, read through its MediaWiki API because the HTML page returns 403 ([API](https://www.pcgamingwiki.com/w/api.php?action=parse&page=Starsiege:_Tribes&format=json&prop=wikitext)); [Tribes Depot wiki](https://wiki.tribesdepot.com/wiki/Starsiege%3A_Tribes) ("2004 (freeware)"); [Frag-Net](https://www.frag-net.com/game_tribes.html) ("Starsiege: Tribes Freeware Release (2004)"); archive.org item description. PCGamingWiki's own reference (gamershell.com) now redirects to an unrelated page. No original 2004 Sierra/VU Games announcement was found or loaded. |
| 2 | Hi-Rez Studios published legacy Tribes games for free download | Verified (archived) | [Tribes Universe, archived 2023-03-14](https://web.archive.org/web/20230314223831/https://www.tribesuniverse.com/): "Join Hi-Rez Studios in celebrating over twenty years of the TRIBES Universe! Below you will find links to download the legacy games." `www.tribesuniverse.com` no longer resolves. PCGamingWiki lists Hi-Rez Studios as the digital publisher. |
| 3 | Hi-Rez's "Starsiege: Tribes" ISO contains Starsiege only | Verified (measured) | `https://webcdn.hirezstudios.com/tribes-universe/iso/StarsiegeAndStarsiegeTribes.iso`, HTTP 200, 545,329,152 bytes, last modified 2021-02-13, SHA-256 `5d7725b0…5db4c`. The volume dates from 1999-08-16. Its readme is "STARSIEGE … V. 1.01", its installer is `AppName=Starsiege`, and neither the image's file list nor its InstallShield file table (`data1.hdr`) names a Tribes executable. |
| 4 | The 2004 freeware installer is `tribes_gsi.exe`, about 135 MB | Verified | Frag-Net links `archive.org/download/Fileplanet_ftp1_052004/…/052004/tribes_gsi.exe` (that path now returns 403). archive.org item [tribesfreeware](https://archive.org/details/tribesfreeware) ("Starsiege: Tribes and Tribes 2 Original 2004 Freeware Release", uploaded 2025-10-20) serves it: HTTP 200, **142,277,508 bytes**, SHA-256 `d91ac445d30154aa45cd468e839bd540feca27ad62b3019a73e6fc4224fbb4a4`. The MD5 (`633a469d…`) and SHA-1 (`e06db6ce…`) match the item's published metadata. |
| 5 | That archive item carries the same freeware lineage TribesPad already uses | Verified | The item's `Tribes2_gsi.exe` SHA-1 `2ba71cb204a2e606bc9cbe6d26f75639cbdcea0d` equals TribesPad's pinned `ref/downloads/tribes2gsi.exe` (`inputs-manifest.json`, 564,721,416 bytes). |
| 6 | `tribes_gsi.exe` is a Wise installer that wraps the retail 1.8 CD and an official 1.8 → 1.11 patch | Verified (measured) | REWise 0.3.1 (`ref/downloads/rewise-c3d3b68903a9`) verifies all CRC32s and extracts 1,773 files (242 MB). `Install/Tribes/` holds the 1.8 tree (257 files, 158 MB). `Patch/tribes18to111.exe` (5,839,208 bytes) is a second Wise installer ("TRIBES 1.8 to 1.11 Update", PatchVersion 1.1.1.6). This agrees with PCGamingWiki, which says the official free download "needs the v1.8 to v1.11 patch". |
| 7 | The patch ships `Tribes.exe` as a binary delta that REWise cannot apply | Verified (measured) | The extracted `Tribes.exe` is 442,730 bytes of non-PE data, and the Wise script records a 2,632,192-byte result. `scripts.vol` and `custom.vol` are deltas too. REWise's README lists "Patch support" as unchecked. |
| 8 | The last official version is 1.11 | Verified | PCGamingWiki; Tribes Depot infobox; the patch's `release.txt` (1.11 added server model authentication against cheats). |
| 9 | Community 1.40/1.41 "was leaked from GarageGames" and later compiled into configs | Verified (one community source) | [shaykbake Tribes wiki, "Playing Tribes"](https://github.com/shaykbake/Tribes/wiki/Playing-Tribes) (mirror: [github-wiki-see](https://github-wiki-see.page/m/shaykbake/Tribes/wiki/Playing-Tribes), last modified 2022-11-26). Tribes Depot says GarageGames bought the IP and source in March 2009. No second source for the leak was loaded. |
| 10 | PlayT1 recommends Groove's 1.41 config, now delivered as an installer `.exe` | Verified | [PlayT1](https://playt1.com/) links `tribes.nyc3.digitaloceanspaces.com/configs/retro_141_installerv2.exe` (HTTP 200, 161,942,399 bytes, last modified 2024-01-17). The page says "Tribes is 100% free and each config comes with the game and a readme". The draft called this a ZIP. The [install guide](https://playt1.com/install) still says to unzip. |
| 11 | Community 1.41 client composition | Verified (install guide) | Per [playt1.com/install](https://playt1.com/install): `Tribes.exe` in the config root; `rasadhlp.dll` beside it; a `plugins` folder of DLLs to compare against the download, because antivirus may quarantine them; `Tribes_win8or10_PluginFix_REG/win8_pluginfix.reg`; a high-DPI compatibility override; VSync off in the GPU driver ("critical"); and a Defender folder exclusion. |
| 12 | How the plugin loader works | Verified | Groove's [PluginFix ZIP](https://twdemos.iateyourbaby.com/twdemos/Groove/140stuff/Tribes_win8or10_PluginFix_REG.zip) (1,559 bytes): the `.reg` changes `HKLM\…\WinSock2\Parameters\AutodialDLL` from `C:\Windows\System32\rasadhlp.dll` to `rasadhlp.dll`, and the readme calls the community `rasadhlp.dll` the "Tribes 1.40+ … pluginloader" that loads plugins such as `scriptGL.dll`. Winsock loads it, and it then loads `plugins/*.dll`. |
| 13 | Other 1.41 distributions exist | Verified | [Tribes 1 JunkYard configs](https://twdemos.iateyourbaby.com/configs.html) lists eight configs (Retro, T1 Basic 2017, ProConfig Vol4, Grey, and others). [shaykbake 1.41](https://github.com/shaykbake/Tribes/tree/main/Installations/1.41) holds "Tribes 1.41 Original by Plasmatic.7z" (95,283,768 bytes, from the defunct tribes0.com). |
| 14 | ModDB "1.41 Basic Config" (`T140Basic.5.zip`, about 140 MB) | **Not loaded** | ModDB returned 403 twice. Kept only as an unverified lead. |
| 15 | Master query protocol | Verified (source and live) | [TheKigen/t1net-go](https://github.com/TheKigen/t1net-go) (Apache-2.0, commit `be356a7`, 2026-04-03), `master.go`, `query.go`, `gamespy.go`, `bitstream.go`. Implemented and run here (see "Live population"). |
| 16 | There are six Tribes 1 masters | Verified | The masters' own MOTD (dated May 3, 2024, signed Kigen): `t1m(1-3).tribes1.co:28000` and `t1m(1-3).pu.net:28000`. `t1m1.kigen.co` resolves to the same IP as `t1m1.tribes1.co` (15.204.204.222), so "both masters" were one host. [tribes1.co](https://tribes1.co/) is a browser front end for T1M1–T1M3. |
| 17 | The 1.8 `Tribes.exe` binary facts | Verified (measured) | See "Binary inventory". |
| 18 | Default 1.8 key bindings | Verified (measured) | Read from the plain-text scripts inside the PVOL `base/scripts.vol`. See "Controls". |
| 19 | 1.11 clients can join today's community servers | Unknown | 14 of 30 servers report the string "1.11" and only one reports "1.41" (see "Live population"), but what the string means for client compatibility is not established. Joining was not tried, by design. |
| 20 | Source code, a decompilation, or an open reimplementation of Tribes 1 | Unknown | None was looked for in depth or confirmed. TribesPad's route does not need one. |

Sources the first draft labelled Verified that this run downgrades: PCGamingWiki's HTML page (403; its content was confirmed through the API instead), the ModDB page (403, unconfirmed), and the gamershell reference (dead). The twdemos archive and the shaykbake wiki did load, but they say something different from the draft. twdemos now offers eight configs, and the shaykbake wiki adds the GarageGames leak and a longer master list.

## Where the game comes from

The rights-cleanest input is the unmodified 2004 freeware installer, pinned by the hashes above. It plays the same role `tribes2gsi.exe` plays for Tribes 2. No official site hosts it today. Sierra and FilePlanet are gone, `tribesuniverse.com` no longer resolves, and Hi-Rez's CDN link delivers the wrong game. The archive.org copy is a preservation mirror of the 2004 FilePlanet distribution, trusted because of its hashes, not because it is an official host. TribesPad should fetch it from the archive.org `tribesfreeware` item, keep the FilePlanet FTP path as a secondary URL (it returns 403 today), and refuse any file whose SHA-256 differs. The Hi-Rez ISO should not be used: it is not Tribes.

The installer's EULA (Sierra, `Install/EULA.txt`) forbids translating, reverse engineering or decompiling the program. TribesPad already treats the Tribes 2 inputs this way, and the same treatment fits here: each player's build downloads the official file and translates it on their own Mac, and nothing derived from the game is published except small patches. `inputs-manifest.json` already records "redistribution authorization has not been established" for the Tribes 2 inputs, and the Tribes 1 entry should say the same.

The 1.11 step is the first open engineering question. The freeware installs 1.8 and then applies a Wise binary patch to `Tribes.exe` and `scripts.vol`. `custom.vol` ships whole in the patch (834,285 bytes, a complete `PVOL` file), so it needs no decoding. TribesPad has two choices, and G0 picks one:

- **Decode the Wise patch format on the Mac** and apply it during input preparation. This is cleanest for players. The format is Wise's own SmartPatch, applied by the 16-bit `WISE0001.DLL` runtime inside `Patch/tribes18to111.exe` ("TRIBES 1.8 to 1.11 Update", PatchVersion 1.1.1.6); the installer carries no third-party differ. Each delta starts with a fixed header (Verified on both deltas): version byte `01`, the new file's DOS date and time, a CRC32 (`0xE0072958` for `Tribes.exe`, believed to be the new file's), the new size (2,632,192, matching the Wise script), a constant `0x19`, the old file's CRC32 (`0xDFA0EBBF`, an exact match for the 1.8 executable) and old size (2,627,072, exact), then what looks like the length of a command section (99,424 bytes for `Tribes.exe`, 206 for `scripts.vol`). Inserted bytes appear unencoded: the `scripts.vol` delta contains the 1.11 `validateShape = true;` lines. The command encoding was then reversed statically from the installer's 16-bit `WISE0002.DLL` (the apply routine at `seg1:14C0`) and ported to [`scripts/research/decode_wisepatch.py`](../scripts/research/decode_wisepatch.py), which reproduces both 1.11 files byte for byte from 1.8 without running any Windows code: `Tribes.exe` 2,632,192 bytes, SHA-256 `e9aa50a43dae8f23d54863be031bc8e1b871afb839d9317b83e372a656c6e740`, and `base/scripts.vol` 964,992 bytes, SHA-256 `d0e70d42fccb48607ec3eb3833994e8134af89ed4d88961af88c648360697db8`. Each matches its header's size and CRC32 (zlib CRC-32, which also reproduces the old-file CRC from 1.8). The decoder refuses to write unless the version, the old size and CRC, every copy's bounds, the consumed command and literal sections, and the new size and CRC all check out. Before that, no open-source decoder was found (REWise lists patch support as missing; WiseUnpacker and Old File Viewer extract files but do not apply patches). The header gives a decoder a fail-closed check at both ends: refuse unless the old CRC and size match, and accept only output with the new size and CRC.
- **Apply it once on a reference Windows or CrossOver install**, record the 1.11 output hashes, and ship a TribesPad-made delta from 1.8 to 1.11. This follows the same small-patch pattern as `patches/translation/`. It needs one run of the official patch on a reference machine, which is the kind of reference setup TribesPad already uses for the Tribes 2 trace.

A third option, shipping 1.8, is weaker. 1.11 added server-side authentication of models, and its readme warns that modified shape files cannot join 1.11 servers, so a 1.8 client is unlikely to be accepted online (Estimate). The 1.9–1.11 crash and cheat fixes would also be missing.

The community 1.40/1.41 client is a separate, later decision. PlayT1 and the other community sites recommend it, but this run found it described as leaked GarageGames code. TribesPad should neither fetch nor translate it until the owner decides with that in view. Its plugin layer adds a second technical cost: plugins patch the running `Tribes.exe`, and TribesPad never runs code generated at run time. Any plugin support would need the finite, captured code revisions TribesPad already uses for the Tribes 2 QoL client.

## Live population

The query script is [`scripts/research/t1query.py`](../scripts/research/t1query.py). It is read-only and runs under Apple's `/usr/bin/python3` with no third-party modules. It sends the t1net-go packets exactly:

- master list: `10 03 FF 00 kk kk 00 00` to UDP 28000, reply `10 06`, packet number and total, the key echoed, `00 66`, then a Pascal name and MOTD, a big-endian server count, and 7-byte records (`06`, four IP bytes, little-endian port);
- per server, GameSpy `62 kk kk` → `63 kk kk 62 …` (name, map, mod, version, players, and per-player ping, packet loss and team);
- per server, native info `10 07 FF 00 kk kk 00 00` → `10 08`, a Huffman bitstream decoded with the t1net-go tree. This is used as a fallback and as a cross-check.

Neither protocol marks AI players. The script counts a player as a probable bot only when the server reports ping 0 and packet loss 0, and labels the count a heuristic. Each run writes a timestamped JSON file to `generated/t1-population/` (ignored by Git, because snapshots hold player names). `--all-masters` also queries the five other masters named in the MOTD.

Results:

| Run (UTC / JST / US Eastern) | Masters queried | Servers listed | Answered | Servers with players | Players | Probable bots | Base-game players |
|---|---|---:|---:|---:|---:|---:|---:|
| 2026-10-22 01:21:57 / 10:21 JST / Wed 21:21 EDT | `t1m1.tribes1.co`, `t1m1.kigen.co` (same host) | 30 | 30 | 6 | 13 | 0 | 4 |
| 2026-10-22 01:52:00 / 10:52 JST / Wed 21:52 EDT | the same two | 30 | 30 | 4 | 9 | 0 | 2 |
| 2026-10-22 01:52:03, `--all-masters` | all six hosts | 30 (union) | 30 | 4 | 9 | 0 | 2 |
| 2026-10-22 02:10:50 / 11:10 JST / Wed 22:10 EDT | the same two | 30 | 30 | 4 | 11 | 0 | 4 |

Servers with players at 01:21: Particle's Custom RPG v3.49 (4, RPG), Rosco's Mixtape 1999 - PlayT1.co (3, base, HillKingLT), TribesRPG.org Main/Worlds (2, RPG), Annihilation Community {OZ} (2, Annihilation mod), x64 Base (1, base, Raindance), Kingdom of Kronos V0.9 (1, RPG). At 01:52: Particle's RPG 4, Rosco's Mixtape 2, TribesRPG 2, Kingdom of Kronos 1. "Base-game players" counts servers whose mod is plain `base`. Every player had a nonzero ping (7–71), so the bot heuristic found none. These are counts of people at that minute, not daily players.

All six masters returned the same 30-server set; the three `pu.net` masters listed 29–30 of them. One server was password-protected. The version strings servers report did not change between runs: 1.11 ×14, "1.1998.107x" ×6, 1.30 ×5, 1.0 ×3, 1.41 ×1, 1.3.12 ×1. What these strings mean for which client can join is Unknown.

In every run all 30 listed servers answered GameSpy. The native Huffman decoder agreed with GameSpy on the player count for all 30 servers in both cross-checks. Map names differed only where GameSpy's Pascal strings cut them off at 16 characters ("Beneath a Steel" against "Beneath a Steel Sky"). The task brief's earlier quick query (01:14 UTC) reported a 363-byte reply from each master. This run's replies were 370 bytes. The masters' list is small enough to fit in one packet.

What this means for the plan: Tribes 1 online play today is a few small evening pickup groups plus RPG servers. PlayT1 says pubs "start around 8pm ET on Rosco's Mixtape" and pickups are organized on Discord. That makes offline bot play the real first product, and it lets online play wait for the rights decision about the 1.41 client without losing much. Three snapshots within an hour on one weekday evening are not a trend. At 02:10 the four servers with players were Rosco's Mixtape 1999 (4, base, HillKingLT), Particle's Custom RPG (4), TribesRPG.org (2) and Kingdom of Kronos (1). G0 adds a weekend and an off-peak run.

## Binary inventory: the 1.8 `Tribes.exe`

All of this is Verified by `pe_inventory.py` (pefile 2024.8.26) and `strings` on the extracted file. Nothing was executed.

- File: `Install/Tribes/Tribes.exe`, 2,627,072 bytes, SHA-256 `e0140d61607f325e9c9259da53f8fa50faff98fb39cfe5b298f7290c7c561382`, PE timestamp 1999-08-02.
- Format: PE32 for i386 (machine 0x14c), GUI subsystem, image base 0x400000, entry RVA 0x1000, SizeOfImage 0x3DF000 (about 3.9 MiB), OS version 1.0. Relocations are present, and the image has a TLS directory.
- Compiler: Borland C++. Linker version 2.25, runtime exports `__GetExceptDLLinfo` and `___CPPdebugHook`, and Borland strings in the image. For comparison, `Tribes2.exe` reports linker 2.50 with no Rich header and no Borland strings (compiler not identified), and IFC22 and Mss32 report 14.44 (MSVC).
- Sections: `.text` 2,146,304 bytes (entropy 6.51, ordinary code, not packed), `.data` 1,736,704 virtual / 324,608 raw, `.tls`, `.rdata`, `.idata`, `.edata`, `.rsrc` and `.reloc` (129,536 bytes). Tribes 2's lifted modules total about 4.64 MB of `.text` (`Tribes2.exe` 3,139,090, IFC22 1,208,516, Mss32 296,340), so Tribes 1 is roughly 46% of that.
- Static imports: 284 functions from 11 DLLs: USER32 110, KERNEL32 92, WSOCK32 25, GDI32 23, WINMM 12, COMCTL32 11, ADVAPI32 5, COMDLG32 2, OLE32 2, SHELL32 1, DSOUND 1 (`DirectSoundCreate`).
- Loaded by name at run time: `opengl32.dll` (the full GL 1.1 table plus WGL, 365 GL/WGL/GLU names in all), `glu32.dll` (only `gluBuild2DMipmaps` and `gluErrorString`), `glide2x.dll` (105 Glide entry points), `smackW32.dll` (13 Smacker video entry points), and `mem.dll` (`MS_Malloc/Free/Realloc/Calloc`, with the message "Failed to load memory library"). `mem.dll` is not in the freeware tree.
- Not used: Miles, DirectInput, DirectDraw and SDL. Audio is DirectSound only, with a `$pref::useDSound3D` switch. A software renderer exists ("Software", GDI DIB sections, palettes).
- Other PE files in the 1.8 tree: `smackw32.dll` (96,256 bytes; imports USER32, WINMM, KERNEL32, GDI32), plus the tools `missionlighting.exe`, `InfiniteSpawn.exe` and `unTribes.exe`, which the game does not need. The tree has no renderer DLLs: OpenGL and Glide support are built into `Tribes.exe` and reach the system DLLs by name.

Unknown until G1: the 1.11 executable's exact imports (the delta adds 5,120 bytes, so large changes are unlikely), the CLSID passed to `CoCreateInstance`, whether `mem.dll` is required or optional, and how much the game uses DirectSound 3D when the pref is on.

## Win32 gap against TribesPad

Method (`/tmp/tribespad-research-t1/coverage.py`): each Tribes 1 import is put in the first class that matches. (A) It has a binding in `port/llasm-runtime/win32-bindings.json` (308 entries) or is named in `config/runtime/*.json`. (B) It is named in `port/core` or `port/apple` sources. (C) It is imported by a Tribes 2 module TribesPad already lifts (`Tribes2.exe`, IFC22, Mss32 and the Miles codecs) but has no binding. (D) None of the above. Class C needs care. The QoL client moved Tribes 2's windowing and display onto SDL3, so many of Tribes 2's own Win32 imports sit in code that never runs and were never implemented.

| DLL | A bound | B source | C T2 import only | D new |
|---|---:|---:|---:|---:|
| KERNEL32 (92) | 68 | 0 | 16 | 8 |
| USER32 (110) | 24 | 0 | 24 | 62 |
| GDI32 (23) | 6 | 0 | 1 | 16 |
| WSOCK32 (25) | 18 | 0 | 4 | 3 |
| WINMM (12) | 0 | 0 | 12 | 0 |
| COMCTL32 (11) | 0 | 0 | 0 | 11 |
| ADVAPI32 (5) | 4 | 0 | 0 | 1 |
| COMDLG32, OLE32, SHELL32, DSOUND (6) | 0 | 1 | 1 | 4 |
| **Total (284)** | **120** | **1** | **58** | **105** |

OpenGL is nearly covered. 350 of the 365 GL/WGL/GLU names Tribes 1 can load are already bound, and three more (`glColor3f`, `glDrawArrays`, `wglGetProcAddress`) are implemented in source. The 12 new ones are `glActiveTextureARB`, `glMultiTexCoord2fvARB`, `glMultiTexCoord4fvARB`, `glTexCoord2fv`, `glVertex3fv`, `glGetTexLevelParameterfv/iv`, `gluBuild2DMipmaps`, `wglGetCurrentContext`, `wglGetDefaultProcAddress`, and the W font variants, all small.

**Estimate of the real gap.** About 163 of 284 static imports (57%) need new work. About 45 of them serve the built-in mission editor and tools: COMCTL32 toolbars and image lists, COMDLG32 file dialogs, menus, dialog boxes and scroll bars. These can stay logged import stops until a player route reaches one, which is how Tribes 2 was handled. That leaves roughly 110–120 functions likely on the play path. Most are small: string, file and time calls, Winsock 1.1 blocking hooks, registry, WinMM mixer and CD-audio volume (stubs), and clipboard and cursor calls. The work concentrates in five areas, which matter more than the count:

1. **A classic Win32 window under GL.** Tribes 1 creates its own window, runs a message pump with hooks, sets display modes with `ChangeDisplaySettingsA`, and builds its GL context with `ChoosePixelFormat` / `SetPixelFormat` / WGL / `SwapBuffers`. On Tribes 2, QoL's SDL3 calls replaced all of this (IFC22 imports 126 SDL3 functions), so TribesPad's window catalog, GL4ES context and message services need a Win32-facing front end. This is the largest new piece.
2. **DirectSound as the only audio path.** `port/core/guest_dsound.c` already creates devices and plays PCM secondary buffers through SDL, but has no 3D voices. Start with `$pref::useDSound3D` off.
3. **Renderer selection.** Make `LoadLibrary("glide2x.dll")` fail, provide `opengl32.dll` and `glu32.dll` natively, and never take the software path.
4. **Smacker video.** Either lift the 96 KB `smackw32.dll` with the same toolchain or answer its 13 entry points natively. The intro movie (`dynlogoF.smk`) can be skipped at first.
5. **Borland code and runtime.** Tribes 1 is the first Borland-built input. The lift preview below measured the translation side, and it held up: the lifter decoded and compiled the whole 1.8 image. What remains Unknown is run time: Borland's exception handling (`RaiseException`, `RtlUnwind`, the unhandled-exception filter) and start-up code have not executed yet.

From the Tribes 2 work, the lifter and toolchain locks, checked memory with hardware permissions, the x87/SSE/MMX exactness work, threads, files, heap, timers, Winsock, GL4ES → ANGLE → Metal, OpenAL/SDL3, the UIKit overlay, packaging and the PadMint recipe shape should carry over. Tribes 2 found its missing services one crash at a time, and Tribes 1 will too, but it starts with 42% of its static imports bound and 353 of its 365 GL names already handled.

## Lift preview: the 1.8 executable through TribesPad's toolchain

On 22 October the retail 1.8 `Tribes.exe` (SHA-256 `e0140d61…`) went through the same pinned tools that translate `Tribes2.exe`, in an ignored scratch folder (`generated/research-t1/`). Nothing was linked or run. This is a translation measurement, not a boot.

| Step | Tribes 1 (1.8) | Tribes 2 for comparison |
|---|---|---|
| Recursive audit (`scripts/audit-code.py`) | 640,112 instructions, 12,509 functions; 0 bad decodes, 0 overlaps, 0 relocation errors | 953,370 instructions, 21,523 functions; 0 / 0 / 0 |
| Uncertain relocations | 2,377 | 102 |
| Unclassified bytes in `.text` | 128,063 (6.0%) | 107,065 (3.4%) |
| SRW with Tribes extensions, diagnostic, full flags | 2.1 s, 440 MB peak; 10 untranslated instructions | 0 untranslated |
| `llasm` to LLVM IR | 315.5 MB of IR in 18 s, after the boundary fix below | |
| `clang -O0` to ARM64 | 36.5 MB Mach-O arm64 object in 36 s, 2.0 GB peak | |
| Unresolved symbols in the object | 375: the 284 Windows imports and 91 runtime helpers | |

The 10 untranslated instructions are all kinds TribesPad has handled before in the QoL client DLL with recorded instruction replacements: add- or subtract-with-carry on high-byte registers (`adc ch, dl` and similar, 8 cases), one `or` from a high-byte register into memory, and one 16-bit `fist word [esp]`. Each traps loudly if reached, so none can run wrong silently.

All 91 runtime helpers the object needs (x87 operations, `rep` string moves, `cpuid`, `rdtsc`, 64-bit divides, `fs:` access, bit tests and shifts) are already defined in TribesPad's runtime or the HaloPad support library it builds on. The CPU side therefore needs no new runtime code. The remaining work is the Windows side measured above, the 1.11 patch, and these 10 instructions.

One Borland-specific fix was needed. Borland packs sections back to back (`.text` ends at `0x60D000`, exactly where `.data` starts; the same at `.tls` and `.rdata`), so SRW emits each boundary label twice: once at the end of one segment and once at the start of the next. `llasm` rejects the duplicate. Dropping the trailing copy, which names the same address, fixed all three boundaries. G2 needs this as a deterministic, recorded preparation step, like the Tribes 2 preparation scripts.

The 2,377 uncertain relocations looked like the main new translation risk (Tribes 2 had 102), so they were classified the same day. 98 are code immediates the audit cannot prove are addresses, the same kind Tribes 2 has. The other 2,279 are real relocation-table entries (the image carries `.reloc`) whose targets are not instruction starts. 2,269 of them point into regions the audit already classed as data inside `.text`: they decode as zero-filled noise, and they are Borland's exception and type-information tables, which Borland keeps in the code section and which point at each other. TribesPad keeps the whole image at its original 32-bit addresses and maps `.text` as data as well, so these pointers stay valid. Of the last 10, four are inline jump tables placed directly after their `jmp [edx*4 + table]` instruction, and the audit already lists all four among its 110 jump tables. Five land one or two bytes inside a hand-written string routine at `0x5DDAC4` (the `0x01010101` byte-scan idiom) that the audit decoded out of alignment, and one points 29 bytes past a `ret` into undecoded bytes. Those six need decode-as-code seeds, the same `fixup_interpret_as_code.sci` mechanism Tribes 2 uses. The relocation risk therefore reduces to about six seeds plus a run-time check of that string routine.


## Launcher design for iPad, iPhone and Mac

Each engine owns one fixed 32-bit guest address space in the app process, and iOS gives an app one process. The launcher is therefore native UI that runs before any engine starts:

- **One process, one engine per launch.** `main()` still calls `tribes_sdl_run_application`. `enter_engine` (`port/apple/mobile_engine_main.m`) first presents a native picker over SDL's window, with Starsiege: Tribes on the left and Tribes 2 on the right, and keeps the main run loop turning until the player chooses. It then prepares that game's paths and calls that engine's entry point (today `tribes_mobile_engine_main`, plus a new Starsiege entry). Nothing from the other engine is initialized.
- **Switching games** is "Switch Game…" in the TribesPad menu. It saves, ends the engine the way Quit already does, and exits. The next launch shows the picker. Restarting the other engine inside the same process is not attempted. On the Mac the app can relaunch itself straight into the picker. Remembering the last choice and skipping the picker can come later.
- **Per-game folders under Application Support.** Today's storage is `Application Support/TribesPad/{Game,Capture,Volume}`, prepared by `tb_mobile_prepare_paths` (`port/apple/mobile_game_paths.m`). It stays exactly where it is, so existing Tribes 2 profiles, prefs and the TribesNEXT account need no migration. Starsiege gets `Application Support/TribesPad/Starsiege/{Game,Capture,Volume}`, also excluded from backup. Logs become `Library/Logs/TribesPad.log` with the game named on the first line, or one log per game. Engine settings are namespaced (for example `TribesPad.Starsiege.onlinePlay`). Touch size, opacity and left-handed layout can stay shared.
- **Two lifted engines in one binary.** Lifted symbols, receipts and the guest-module tables must not collide (Estimate: give each engine its own symbol prefix and keep one shared `port/core`). The Tribes 2 hardware-permissions engine is about 64 MB. Tribes 1's code is about 46% of that, but the size has to be measured before committing to one app.
- **Shared touch code with per-game binding tables.** `port/apple/mobile_controls.m` hard-codes Tribes 2 in four places (line numbers at commit 4b62e07): the button spec at lines 413–420, the state file path `TribesPad/Game/base/prefs/tribespad-state.txt` at lines 441 and 645, the controller map at lines 539–561, and the action menu at lines 744–761. Move these into one static profile per game: buttons and their keys, the controller map, the menu actions, chat, console and inventory keys, and the state file path. Choose the profile when the engine starts. The layout, analog stick, look surface, trackpad cursor, menu shell, settings and release-on-background logic stay shared. This is a pure refactor that must leave Tribes 2's behavior identical, and it is checked by replaying the existing Tribes 2 touch route.

## Controls

**Verified, Tribes 1 (1.8) defaults** from `base/scripts.vol`: W/A/S/D move, Space jumps (skiing is held jump on slopes), right mouse jets, left mouse fires, X crouches, R toggles the view, F holds free look, E and Z control the zoom, 1–9 select weapons directly (Blaster, Plasma Gun, Chaingun, Disc Launcher, Grenade Launcher, Laser Rifle, ELF gun, Mortar, Targeting Laser), Q and Shift+Q are next and previous weapon, G and M are pressed and released to throw a grenade or mine, B is beacon, H is repair kit, P uses the pack, I/C/O open the inventory, command and objectives screens, V is the chat menu, Ctrl+K is suicide, Ctrl+F drops the flag, Ctrl+W/A/P drop the weapon, ammo or pack, Shift+F1–F5 buy favorites, and Ctrl+Y/N vote. Chat-to-all, team chat, scores and console keys were not found in `scripts.vol` and remain Unknown. The 1.11 deltas may also change bindings.

The touch layout maps cleanly onto this. Fire and Jet stay on their own buttons (Jet sends right mouse, not a key). Ski / Jump sends Space. Weapon sends Q (long press for Shift+Q, or a wheel over 1–9). Grenade sends a G press and release. Pack sends P. Inventory sends I and hides the controls, the same as Tribes 2. The menu gets Repair Kit (H), Mine (M), Beacon (B), Command (C), Objectives (O), Zoom (E), View (R) and Suicide (Ctrl+K). The match-state signal Tribes 2 gets from `tribespad-state.txt` needs a Tribes 1 script hook. A TribesPad-owned `autoexec.cs` that writes a file on mission load and leave is the Estimate, settled at G4.

## Gated plan

Each gate ends with a short record under `docs/validation/` and passes only on its stated evidence. A build, an install or a process start is not gameplay.

**G0: inputs, rights and population (desk work, no engine).**
Pin `tribes_gsi.exe` (archive.org primary, FilePlanet path secondary, SHA-256 above). Choose the 1.11 route (decode the Wise patch, or apply it on a reference machine and record the output hashes). Record the 1.11 `Tribes.exe`, `scripts.vol` and `custom.vol` hashes. Ask the owner for a decision on the 1.40/1.41 client's provenance. Run `scripts/research/t1query.py --all-masters` on a weekend evening and on a weekday off-peak, and record the results.
**Status 22 October:** the reproducible 1.11 step is done (decoder and hashes above). The 1.11 `Tribes.exe` has the same 11 DLLs and 284 imports as 1.8, links on 26 April 2000, and contains `requires version 1.11 or higher`. Remaining for G0: pin `tribes_gsi.exe` in the inputs manifest, the 1.41 decision, and two more population snapshots.
*Continue* when the freeware input and a reproducible 1.11 executable exist. *Stop* if 1.11 can only come from an unverifiable third-party build. *Offline-only scope* if online stays limited to a handful of people and the 1.41 decision is negative.

**G1: binary census of 1.11.**
Run the same PE inventory on the 1.11 executable. Decide how to handle `smackw32.dll` (lift or native). Find the `CoCreateInstance` CLSID and whether `mem.dll` is required. Rerun the coverage table.
*Continue* if the image is unpacked and the import set is within about 10% of 1.8's. *Stop* if the code decrypts or generates itself at run time.

**G2: lift and reach the main menu on Mac.**
Lift `Tribes.exe` with the pinned toolchain, with the section-boundary label fix as a recorded preparation step and the 10 high-byte and `fist word` instructions replaced as in the QoL client. Add the Win32 window, GL pixel-format and WGL front end, and the remaining services, until the original menu renders through GL4ES/ANGLE. Glide must be refused and DirectSound 3D off.
*Continue* when the menu is visible and the session log has no abort stops. *Stop and re-plan* if the import frontier is still growing after two weeks of focused work.

**G3: offline match on Mac.**
Host a local game with bots. Spawn, move, jet, ski, fire, change weapons and inventory, finish the mission, and quit cleanly. Audio must be audible through OpenAL. Compare one recorded flight against the original 1.11 under CrossOver, the way Tribes 2 was checked.
*Stop* on a trace divergence that cannot be explained.

**G4: controls and the Tribes 1 profile.**
Do the per-game profile refactor first and replay the Tribes 2 touch route unchanged. Then build the Tribes 1 profile from the bindings above, find the missing chat, score and console keys, add the match-state hook, and map the controller. Play with real touches in the Simulator, then on a physical iPad. Get confirmation before driving a device that is in use.

**G5: online, read-only first.**
Show the original server browser with the community masters. Joining a public server needs the owner's explicit approval for Tribes 1, because the Tribes 2 approval does not carry over. 1.41-only servers stay out of scope unless G0 approved that client.

**G6: launcher and release.**
Build the picker, the per-game folders and Switch Game. Measure binary size and memory with both engines linked. Run the one-command build from a clean clone, and PadMint's `scripts/player-check.sh` with Apple's Python and an empty PadMint folder, as AGENTS.md requires. Tribes 2 must show no regression: run its offline Katabatic route and a device launch with an existing profile preserved. Release and any announcement need separate approval.

## First concrete milestone

Finish G0 and G1 without touching the engine. The reproducible 1.11 `Tribes.exe` is done, and its lift preview matches 1.8 (641,074 instructions, the same 10 untranslated instructions, a 36.6 MB ARM64 object). Still to do: pin `tribes_gsi.exe` in the inputs manifest; add the six decode seeds; write a one-page validation record with the 1.11 PE inventory, the Smacker and `mem.dll` decisions and two more population snapshots; and get the owner's answer on the 1.41 client. That settles the input and rights questions and sizes G2. The first engineering step after it is the Win32 window, pixel-format and WGL front end, because no Tribes 1 frame can appear without it.

## Reproducing this research

The query script is in `scripts/research/`. The rest was scratch work on the maintainer's Mac in `/tmp/tribespad-research-t1/`: the `snapshot-*.json` files, `pe_inventory.py` and `pe-inventory.json`, `coverage.py` and `coverage.json`, the t1net-go checkout, the downloaded `tribes_gsi.exe` and Hi-Rez ISO, and the REWise extractions `t1extract/` and `t1patch/`. Copies of the loaded pages are in `src/`. None of it belongs in the repository.

