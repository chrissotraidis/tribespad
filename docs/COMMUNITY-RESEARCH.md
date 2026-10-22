# TribesPad community research: what players want and what the community exposes

Research date: 2026-10-22. Scope: read-only research of public pages and services; no one was contacted.

Labels: **Verified** means I fetched or ran it today. **Estimate** is a judgment or inference. **Unknown** means I could not establish it.

## 1. What players asked for

Sources: the r/Tribes post "I got Tribes 2 running natively on Mac, iPhone and iPad" (22 comments when read on 22 October 2026) and the PlayT2 Discord discussion the same day. Comments are summarized, not quoted by name.

| Theme | What people said | What it means for TribesPad |
|---|---|---|
| Performance | The top comment suggested turning precipitation off for frames; others said a lower render resolution would help a lot. | Add a render-scale option and a precipitation toggle; measure frame rate on devices. |
| Playing together | "Let's get some multiplayer going", plans to try it on a weekend, wanting to learn the iPhone install. | A clear place and time to play (Find a Game, Game Nights) and a simpler install guide. |
| Other platforms | Requests for Raspberry Pi and Android builds; a dedicated server on a Pi; GameNative was mentioned as a way to run Tribes 2 on Android today. | See [ANDROID-LINUX-PLAN.md](ANDROID-LINUX-PLAN.md) and [DEDICATED-SERVER-PLAN.md](DEDICATED-SERVER-PLAN.md). |
| How it works | Whether it is emulation or a port. | The README's "Recompiled, not decompiled" section answers this: native ahead-of-time recompilation, not emulation and not a source port. |
| Goodwill | Nostalgia, LAN-party memories, "Shazbot in 2026". | |

No one reported a crash or a build failure. No one asked for chat, accounts or stats. The concrete asks were faster rendering and a way to find people to play with.


## 2. GitHub: chrissotraidis/tribespad

Verified with gh: there are **0 issues**, and Discussions are **disabled**. The repo has 5 stars and 0 forks. Of the 23 PRs, all are by the owner: #1–#21 and #23 are merged, and **#22 is open**.

PR #22, "Find a Game: who's playing now, game nights, community; Skins folder; 16-bit textures" (opened 2026-10-22), already adds most of the planned community surface:

- **Who's Playing Now** reads https://master.tribesnext.com/list and sends each server the original UDP ping (14→16) and info (18→20) queries. It counts people and bots separately, lists servers with people first, and shows mission, type, ping and a password lock. Tapping a server joins it through prefs/tribespad-request.txt, which autoexec polls every 200 ms.
- **Game Nights**: Wednesday 9:00 PM, Saturday 9:30 PM and every other Sunday 9:30 PM US Eastern (from tribes2forever.com), shown in local time.
- **Community** links: the PlayT2 Discord (discord.gg/EMMVhQJgf9), The Cut (discord.gg/yz3AhkJCxg), the TribesPad Discord (discord.gg/xwHfUD2bxW) and the TribesNEXT forum.
- A Skins folder in Files.
- Not yet exercised on a device: the menu itself, Join from the sheet, the Skins copy, and 16-bit textures.

Implication: player feedback is not reaching GitHub, so Reddit and Discord are the real feedback channels today. Enabling Discussions, or adding an issue template that asks for TribesPad.log, costs little (Estimate).

## 3. Community hubs and what they expose

| Hub | Programmatic surface | Status / notes |
|---|---|---|
| TribesNEXT master, plain list | GET https://master.tribesnext.com/list returns ip:port lines | **Verified**: 29 servers, HTTP 200, Cloudflare |
| TribesNEXT master, JSON | GET https://www.tribesnext.com/list.json | **Verified**. Returns application/json with access-control-allow-origin: * and cache-control max-age=120. Fields per server: s_ipa, info_hostname, info_mod, info_map, info_maptype, info_desc, info_flags{dedicated,password,linux,tournament,no_alias,max_players}, info_ping, num_players, num_bots, info_players (team rosters with names and scores), time_added, time_queried. PlayT2's ticker uses it with a 60 s refresh, and TribesPad's old diagnostic shell (port/apple/TribesPad.m) already fetches it. |
| TribalOutpost live servers | https://tribaloutpost.com/tribesnext/servers (server-rendered HTML; filters by mod and game type) | **Verified**: "11 players online, 6 active servers, 29 servers found", built from the TribesNEXT master |
| TribalOutpost stats | https://tribaloutpost.com/stats: PUG sessions, leaderboards, match history | **Verified** page: modern era (2026+) has 2,188 matches, 627 players, 8 servers and 1.16 M events. Recent sessions: PUG #108 (Oct 22, 3 matches, 36 players), #107 (Oct 22, 19 players), #106 (Oct 18, 19 players). **No public JSON API found** (/api/stats returns 404, so this is Unknown). Using it would mean scraping HTML; ask the site first. |
| TribalOutpost league | https://tribaloutpost.com/league/25th-anniversary (teams, stats) | **Verified** links exist |
| TribalOutpost maps and AutoDownload | API: GET /api/autodownload/resolve?filename=…&display_name=… returns {found, slug, title, file_size, download_url}. GET /api/adl/verify/{type}/{slug} returns {sha256, signature (Ed25519), key_id}. | **Verified**: resolve answers JSON ({"found":false} for my guessed filename). The client is https://github.com/sfphinx/tribaloutpost-auto-downloader (MIT, Go, beta v1.0.2). Its T2 script hooks the "missing file" MessageBox and writes request.adl/response.adl, the same file-handshake pattern as PR #22. ToS (Feb 2025): personal use is allowed, as is "Use the AutoDownload API for Tribes 2 servers". Automated mass download is prohibited, and creators keep ownership. https://tribaloutpost.com/terms |
| TribesNEXT asset depot | https://www.tribesnext.com/files/depot.txt lists size, MD5 and path | **Verified**: 11,032 entries, last modified 2025-10-08. TribesPad already turns the depot on by default. Licensing: no explicit license found; the files are community-made content (Unknown). |
| TribesNEXT forum | Vanilla RSS: https://www.tribesnext.com/forum/discussions/feed.rss | **Verified**, but low traffic: newest posts are 2026-10-21 ("IP Error - Can't connect…") and 2026-09-02 |
| TribesNEXT accounts, tribes and clans | t2csri community certificates (name, clan, tag, membership), signed by a delegated server and fetched through the in-game HTTPObject at www.tribesnext.com "auth" | **No public profile or clan API found** (Unknown). The data only flows through the signed in-game login. |
| PlayT2 (playt2.com) | Site ticker reads list.json; the Discord has a Server Query Bot, a PUG Bot and game alerts | **Verified**: ticker code fetches list.json. The bots are described in the TribesNEXT forum thread https://www.tribesnext.com/forum/discussion/4235/tribes-2-discord (via web search). Discord: 581 members, 219 online. |
| The Cut / tribes2forever.com | Static Next.js page: 25th Anniversary draft tournament and schedule | **Verified**: Wed 9:00 PM EST PUG, Sat 9:30 PM EST PUG, Sun 9:30 PM EST CTF pub every other week. The page still says "Next: May 24th", so the text is **stale**. It claims "200+ active players, 400+ matches/month". The Cut Discord: 367 members, 103 online. |
| TribesPad Discord | discord.gg/xwHfUD2bxW | **Verified**: resolves to the guild "Kahris", 794 members, 109 online. It is a shared server, not a TribesPad-only one. |
| Discord bots | BattleBot (https://github.com/amineo/discord-battlebot), MIT, Elixir, updated 2026-09-26, built for the Tribes 2 community: /query t2 for live servers and /events; its topic-monitoring alerts are marked as coming soon in its README | **Verified** repo; whether PlayT2 runs this exact bot is Unknown |
| Discord widgets | /api/guilds/{id}/widget.json | **Verified disabled** (code 50004) on PlayT2, The Cut and Kahris |
| Discord invite counts | GET https://discord.com/api/v10/invites/{code}?with_counts=true returns approximate member and online counts, no auth | **Verified** for all three |

**Live snapshot (Verified, 04:32 UTC, about 12:30 AM US Eastern on a Thursday):** list.json reported 144 players, of whom 133 were bots. That leaves **11 people on 2 servers**: TRIUMPH BotAbuse had 10 people plus 16 bots, and The-Construct had 1. TribalOutpost showed the same "11 players online". Caveats for anyone building on this feed:

- num_players **includes bots**, so people = num_players − num_bots.
- Rosters do not mark which entries are bots. TRIUMPH listed 26 bot-style names while reporting 16 bots.
- Hostnames carry HTML entities (&#39;) and mojibake (Â).
- info_ping is measured from the master server, not from the player, so per-device UDP pings are still needed.

## 4. Chat options

- **In-game CHAT tab (IRC):** QoL's IFC22.dll hardcodes irc.tribes2.net:6667. **Verified today:** it resolves to phoenix.tribes2.net (15.204.87.71) and sends a welcome, then "468 NO IRC ATM". The built-in chat is effectively off. The only other IRC reference I found is one server description pointing to #the_construct on irc.quakenet.org. Whether a QuakeNet #tribesnext channel exists or is active is Unknown. **Do not build on IRC.**
- **Discord is where the community talks** (PlayT2, The Cut). The cheapest useful steps are invite buttons that open the Discord app, with live online counts from the invite API. iOS universal links from discord.gg opening the app is an Estimate.
- **Rich Presence:** QoL ships console_client_discord.cs plus discord_game_sdk.dll. The script runs only when that Windows DLL loads. It sets state to the server name, details to gametype (mod), and party size and max. Its joinSecret = Base64(ip + " " + $JoinGamePassword), which means **the server password goes into the invite secret**. It also handles invites and join requests. TribesPad does not translate that DLL (README lists Tribes2.exe, the QoL DLL and Miles), so the script stays inactive (Estimate from the README). Discord has archived the legacy Game SDK ([github](https://github.com/discord/discord-api-docs/blob/main/developers/developer-tools/game-sdk.mdx)), and new work goes into the Social SDK, which added mobile and iOS support with general availability in v1.10 ([Discord blog](https://discord.com/blog/mobile-platform-support-arrives-in-discord-social-sdk), [release notes](https://discord.com/developers/docs/social-sdk/release_notes.html)). A native port would require a Discord application ID, account linking and native SDK work. That is high effort for a small audience, so defer it.
- **Native chat without accounts:** none is available. Building a TribesPad chat server would mean moderation and identity work and would fragment an already small community. Do not build it.

## 5. What a no-account Community panel can show, with sources

| Card | Source | Refresh |
|---|---|---|
| "N people playing now" plus servers with people first (map, mode, mod, lock, your ping) | list.json, plus UDP ping for your own latency | 60–120 s (matches the server cache) |
| "Next game night: Sat 9:30 PM ET, in 5 h" with a Remind me button | Schedule JSON hosted by TribesPad (raw GitHub file), first seeded from tribes2forever | On launch. Local notification through UNUserNotificationCenter, no server needed. Handle DST: US clocks change on 2026-11-01. |
| Discord: PlayT2 219 online, The Cut 103 online | Discord invite API | On open |
| Recent PUGs: "#108, 36 players, last night" | TribalOutpost stats (HTML only; ask the site first) | Hourly |
| News | TribesNEXT forum RSS plus TribesPad GitHub releases | Daily |
| Friends playing | Local list of warrior names matched against list.json rosters | With the server list. Names are unauthenticated and not unique. |

## 6. Ranked ideas (value for a phone/tablet player ÷ effort)

1. **Base Who's Playing Now on list.json** (high value, low effort). One HTTPS request replaces the UDP fan-out for names, mods and counts. Keep UDP only for your own ping, decode HTML entities, and show "people / bots" honestly. This also feeds the planned launcher: "11 people playing · game night in 5 h" on the Tribes 2 card.
2. **Game Nights with countdown and local reminders, from an updatable schedule file** (high value, low effort). This directly answers "let's get some multiplayer going" and "try it next weekend", and it avoids hardcoding stale schedule text.
3. **Discord buttons with live online counts** (medium value, very low effort). This is the chat answer for now.
4. **Map auto-download through the TribalOutpost API** (high value, medium effort). It is already on the roadmap. Popular servers run custom maps (TRIUMPH "300Maps", TWL maps in PUGs). The depot covers interiors; TribalOutpost covers map .vl2 packs, with signed hashes. Reuse the file handshake (missing-file dialog → native resolve → verify → install → rejoin). Credit TribalOutpost, and download only on demand.
5. **Render scale and a precipitation toggle in Settings** (high value, low to medium effort). These are the top Reddit asks. They are not community features, but they decide whether online matches feel good.
6. **Friends / favorite warriors and favorite servers** (medium value, low to medium effort). Highlight servers where saved names appear.
7. **Recent PUGs and leaderboard card** (medium value, medium effort). It depends on TribalOutpost permission or an API.
8. **Feedback channel** (low value per player, very low effort). Enable GitHub Discussions or an issue template that asks for TribesPad.log, and put a "Report a problem" item in Help.
9. **Defer:** Discord Social SDK Rich Presence (high effort). **Skip:** IRC chat (the server says no IRC) and a custom chat backend.

For Starsiege: Tribes in the launcher, I did not research Tribes 1's master servers or community. That remains Unknown and needs a separate pass before the launcher shows Tribes 1 activity.

## Sources

- Reddit thread: https://www.reddit.com/r/Tribes/comments/1xbme78/i_got_tribes_2_running_natively_on_mac_iphone_and/
- TribesNEXT: https://master.tribesnext.com/list, https://www.tribesnext.com/list.json, https://www.tribesnext.com/files/depot.txt, https://www.tribesnext.com/forum/discussions/feed.rss, https://www.tribesnext.com/forum/discussion/4235/tribes-2-discord
- TribalOutpost: https://tribaloutpost.com/tribesnext/servers, https://tribaloutpost.com/stats, https://tribaloutpost.com/autodownload, https://tribaloutpost.com/terms, https://github.com/sfphinx/tribaloutpost-auto-downloader
- PlayT2: https://playt2.com/ · The Cut: https://tribes2forever.com/
- Discord: https://discord.com/api/v10/invites/{code}?with_counts=true; Game SDK archived: https://github.com/discord/discord-api-docs/blob/main/developers/developer-tools/game-sdk.mdx; Social SDK mobile: https://discord.com/blog/mobile-platform-support-arrives-in-discord-social-sdk, https://discord.com/developers/docs/social-sdk/release_notes.html
- BattleBot: https://github.com/amineo/discord-battlebot
- Repo (read-only): PR #22 diff (port/apple/live_servers.m), ref/qol/console_client_discord.cs, ref/qol/IFC22.dll strings, t2csri scripts

