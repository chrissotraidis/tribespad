# Performance on iPad and iPhone: evidence and plan

Last updated 24 October 2026. This records what the device logs show, what changed because of them, how to measure the next session, and what is still worth trying. Numbers come from `Library/Logs/TribesPad.log` copied off the device with `xcrun devicectl device copy from`; nothing here was measured in the Simulator unless it says so.

## What a real match looks like today

Device: iPad Pro 12.9-inch (6th generation, M2), iOS 27.0.1, a build of PR #22 with the slow-stretch log. Session of 22 October, 14:24 JST, online.

| When | Frames per second | Longest frame | Frames over 50 ms in 10 s |
|---|---:|---:|---:|
| Start-up (first 10 s) | 74.0 | 2,683 ms | 3 |
| Menus | 103.5 | 379 ms | 7 |
| Joining and loading | 62.3 | 1,533 ms | 13 |
| In the match | 21.6, 31.0, 23.8, 25.6 | 254–1,458 ms | 22–44 |

The menus run well past 60 FPS at full resolution, so presenting frames is not limited by itself. In a match the game drops to about 25 FPS with frequent stutters. The 120 Hz screen does not cap anything (`max_fps=120` in the log), and on iOS ANGLE's Metal backend fixes the swap interval at 1, so the game's VSync option has no effect there.

The game draws at the screen's full resolution: the log reports a content inset of 0 and no density override, which means 2732 × 2048 pixels, 5.6 million per frame. Phones already draw at a reduced size (`port/apple/content_area.m`); iPad did not.

The player profile on that iPad is above the game's own High preset in every costly setting:

| Setting (`ClientPrefs.cs`) | This iPad | Dynamix High | Dynamix Medium |
|---|---|---|---|
| `$pref::Terrain::screenError` (lower = more terrain triangles) | 0 | 8 | 20 |
| `$pref::Terrain::texDetail` (lower = more blending work) | 0 | 1 | 3 |
| `$pref::TS::detailAdjust` (shape detail) | 1.77 | 0.83 | 0.68 |
| `$pref::Interior::detailAdjust` (building detail) | 1.64 | | |
| `$pref::numCloudLayers` | 3 | 1 | 0 |
| Decals, environment maps, building lights | on | on | off |
| Shadows | 0 | 0.5 | 0.1 |
| Precipitation | off | on | off |

QoL widens the game's sliders (`ref/qol/console_client_patches.cs`: shape detail up to 2.0, building detail 0.3–2.0), so values above Dynamix's range are reachable from the Options screen. Terrain `screenError` 0 is the most expensive terrain setting the game has: Torque tessellates terrain on the CPU every frame, and TribesPad runs that code translated, with checked memory.

Earlier logs (21 October) showed two more costs, both already addressed: every texture update was written to the log (18 MB, 160,000 lines in one session; capped at 16 per session since `f11741b`), and the terrain blender re-uploaded a 128 × 128 16-bit texture with its whole mip chain about 17,000 times, which GL4ES widened to 32-bit on the CPU each time (fixed in PR #22 with `LIBGL_AVOID16BITS=0`).

What the logs cannot say yet is whether the in-match slowdown is the CPU (the game's own work) or the GPU (drawing 5.6 million pixels through GL4ES and ANGLE). The next build measures that.

## Why every profile started at maximum detail

Found 24 October. Tribes 2 picks a graphics profile at first launch by matching the GPU's vendor and renderer strings against `CardProfiles.cs` (Voodoo, Rage, GeForce, Radeon and so on), and the matching profile file (for example `HighProfile.cs`) sets the detail prefs. No entry matches Apple's GPU through GL4ES and ANGLE, so no profile runs and the detail prefs stay empty. The Options screen then turns empty values into its most expensive settings when it saves: terrain `screenError` 0 (25 minus a full slider) and `texDetail` 0. A profile made by a fresh Mac test launch showed exactly the values on the iPad (terrain 0, texture detail 0, shape detail 1.77, building detail 1.64, three cloud layers), so every TribesPad player has started this way.

A second quirk keeps settings high: shape detail and shape distance share one Options slider, and QoL widened the detail end of it to 2.0. Options recomputes `TS::detailAdjust` from the slider every time it saves, so closing Options with the High profile's shape distance gives shape detail 1.77. The first version of the presets (22 October) set Dynamix's pair and would have drifted back up the next time Options closed.

Fixes in this branch: new profiles start at Balanced (`TribesPadDetail` in the generated `base/autoexec.cs`, applied only when no `prefs/ClientPrefs.cs` exists at start-up); both presets use a shape-detail and shape-distance pair the slider reproduces, so Options keeps them; and existing profiles at maximum detail get a one-time offer to switch to Balanced after login.


## What changed

| Change | Where | Effect | Status |
|---|---|---|---|
| Texture-update logging capped | `port/core/guest_sdl_backend.c` (`f11741b`) | No more per-upload log writes | In `main` |
| FPS counter in ··· with the slowest frame, "Hot" when throttled; slow-stretch and thermal lines in the log | `port/apple/mobile_controls.m`, `port/apple/gl4es_context.c` | Measurement | In `main` |
| Background time no longer counted as a slow frame | `gl4es_context.c` | A suspended app logged a 291-second "frame" | PR #22 |
| 16-bit textures stay 16-bit | `gl4es_context.c` | Removes a CPU format conversion per terrain upload | PR #22; terrain colours not yet checked on device |
| **Render Scale** (Settings > Display, 50–100%) | `content_area.m`, `mobile_controls.m` | 75% draws 56% of the pixels, 50% a quarter. Menus keep their size because the UI scale follows the pixel height | This branch; applies at the next launch |
| **··· > Graphics > Balanced / Fast** | `mobile_controls.m`, the generated `base/autoexec.cs` (`scripts/build-ipad-engine.py`) | Balanced is Dynamix High detail with one cloud layer, light shadows and no weather; Fast is Dynamix Medium. Applied live through the request file, as Options would, and saved | This branch |
| **GPU wait in the slow-stretch line** | `gl4es_context.c`, `port/core/guest_sdl_backend.c` | Each line ends "waiting for the GPU X of Y ms per frame": swap plus QoL's frame-latency fence and the GL4ES stream-ring waits. Swap time alone read 0.0 ms on the iPad, because ANGLE returns from swap at once on iOS | This branch |
| **Balanced defaults for new profiles; presets that survive Options; one-time offer** | generated `base/autoexec.cs`, `mobile_controls.m` | See the section above | This branch |

Nothing changes a player's settings without them choosing it: Render Scale defaults to 100%, and the presets ask before applying.

## How to measure the next session

1. Turn on **··· > FPS Counter** and play a few minutes of a real match.
2. Then try **··· > Graphics > Balanced** in the same match and watch the counter.
3. Set **Settings > Render Scale** to 75%, reopen TribesPad and play again.
4. Copy the log off the device: `Library/Logs/TribesPad.log` in the app container (`xcrun devicectl device copy from`, or attach it to a GitHub issue).

Reading a slow-stretch line: `25.0 FPS ... waiting for the GPU 4.1 of 40.0 ms per frame` means the game spent about 36 ms of each 40 ms frame on its own work, so the CPU is the limit; lower detail (Balanced or Fast) helps, Render Scale does not. `waiting for the GPU 30.0 of 40.0 ms` means the frame waited on drawing; Render Scale helps. The line is written for every 10-second stretch under 50 FPS or with a frame over 250 ms.

## Still worth trying, in order

1. **Choose defaults from the measurements.** If Balanced or a lower Render Scale gives a clear gain, make it the first-run default for new profiles (existing players keep their settings). Decide only after a device session, since it changes how the game looks.
2. **QoL's low-latency option.** `$pref::Video::LowLatency` is 1 on this iPad. In the Simulator about 14% of the game thread waited in QoL's `glClientWaitSync` frame fence (HANDOFF). Try it off and compare the GPU wait in the log. Unverified that the option controls that fence.
3. **A device CPU profile.** An Instruments Time Profiler capture of a match on the iPad would show whether terrain, the checked-memory paths, x87 SoftFloat (about 7% in the Simulator), the GL staging checks or the SDL bridge dominate. HANDOFF lists rejected experiments (inline grouped checks, sticky readers, GL4ES batching); start from the profile, not from those.
4. **GL4ES mip uploads.** Count the `glTexSubImage2D` calls GL4ES sends ANGLE per terrain update; more than the game's 8 levels would mean GL4ES regenerates mips the game already supplies.
5. **Start-up and map-load hitches** (2.7 s at start, 1.5 s on joining). Those are datablock and texture loading; a cache would help repeat joins.

## Beyond this iPad

Other directions are planned separately: a headless dedicated server on a Mac, then Linux on ARM ([DEDICATED-SERVER-PLAN.md](DEDICATED-SERVER-PLAN.md)); Raspberry Pi 5 / Linux and Android builds ([ANDROID-LINUX-PLAN.md](ANDROID-LINUX-PLAN.md)); community features ([COMMUNITY-RESEARCH.md](COMMUNITY-RESEARCH.md)); and Starsiege: Tribes ([STARSIEGE-TRIBES-PLAN.md](STARSIEGE-TRIBES-PLAN.md)). Device performance comes first, because every one of those builds on the same engine.

