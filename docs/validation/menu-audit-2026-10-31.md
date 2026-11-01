# ··· menu audit: Starsiege: Tribes and Tribes 2

31 October 2026, against `main` at `e828817`. Source read: `rebuildMenu`, the
handlers it calls, the Settings sheet and the first-play hints in
[mobile_controls.m](../../port/apple/mobile_controls.m). Both games share one
overlay; `tb_starsiege` swaps in Starsiege's own sections. The Mac has no ···
menu: both Mac engines use the keyboard, mouse and the games' own menus.

## Order and sections

Both games already build the same order: **This Match** (on a server), **Play
Online**, **Options** and **Help**, then **Choose Game** when Starsiege is in the
build. Off a server, Play Online leads and opens inline. This stays as it is.

| Section | Tribes 2 | Starsiege: Tribes | Verdict |
| --- | --- | --- | --- |
| This Match: game menu | Game Menu in play, Back to Game in menus (Escape after hiding the controls and keyboard) | Always Game Menu; raw Escape with the controls left up | **Fixed:** Starsiege uses the same item and handler |
| This Match: Scores | F2 | Tab | Each game's own key |
| Chat: All / Team | U / Y, keyboard opens | T / Y, keyboard opens | Same behavior |
| Chat: Quick Chat Menu | V, keyboard opens to pick by letter | V only; a touch player could not pick an entry | **Fixed:** Starsiege opens the keyboard too |
| Actions | Repair kit, mine, beacon, laser, throw flag; zoom, third person, command map; suicide | Inventory, objectives, command map; repair kit, beacon, drop flag; zoom, third person; suicide | Each game's own actions and names; kept |
| Play Online | Who's Playing Now, Server List, Game Nights, Community | Same four, with Tribes 1 sources and links | Same items and behavior |
| Options | Touch controls, trackpad, FPS, Graphics Quality, Settings | No Graphics Quality | Correct: the presets exist only in Tribes 2's `autoexec.cs` |
| Help: Controller | "Controller: <name>" when one is connected; guide titled with it | Always "Controller Guide"; guide said Menu opens the ··· menu, but it opens the game menu | **Fixed:** same name and status in both; Starsiege text corrected |
| Help: Console | Grave, hides the controls, opens the keyboard | Grave only; no keyboard to type with | **Fixed:** Starsiege uses the same handler |
| Help: GitHub, About | Same | Same, with Starsiege's own About text | Same |
| Choose Game | Subtitle "Tribes 2 or Starsiege: Tribes" | "Starsiege: Tribes or Tribes 2" | Fine for two games; needs one wording for three |

## Around the menu

| Behavior | Before | After |
| --- | --- | --- |
| First-play controller hint | Starsiege showed Tribes 2's layout (LB targeting laser, B place mine) | Each game shows its own layout |
| "Looking for a game?" hint | Tribes 2 only | Both games; the Balanced graphics offer stays Tribes 2's |
| Station hint (Starsiege) | Pointed at "Game Menu", which is now named Back to Game there | Points at Back to Game |
| Settings sheet | Starsiege showed Keep Clear of the Notch, Render Scale, WASD Movement and Online Play, none of which change Starsiege (fixed 800x600 frame, its own WASD keys, no TribesNEXT login) | Starsiege shows only the rows that change it; the Display footer describes its frame |

## Checks

The overlay compiles for iOS device and Simulator with `-Wall -Wextra -Werror`
(the UIKit step of `scripts/test-mobile-controls.py`). The script's first step,
`tests/mobile_controls_test.c`, already fails on `main`: it calls
`tb_mobile_controls_gamepad_look` with four arguments after the function gained
a fifth. On-device checks still to do on the iPad and iPhone, in each game:
open every ··· item in a match and in menus; type in Starsiege's console and
quick chat menu with the on-screen keyboard; open Settings and Reset to Defaults;
connect a controller and read the hint and guide.

## Left for the three-game picker

- The picker shows Tribes: Vengeance as **In development**, and Settings' **Game
  Picker at Launch** now reads "Choose your game when TribesPad opens". Choose
  Game's subtitle and the gear menu's **Open Tribes 2 Directly** still name the
  two playable games; they change when Vengeance plays. See the
  [Vengeance plan](../VENGEANCE-IMPLEMENTATION-PLAN.md#card-stages).
- Game-specific menu sections branch on one `BOOL`. A third game should first
  move them into a per-game table (the profile in
  [LAUNCHER-PLAN.md](../LAUNCHER-PLAN.md#3-shared-controls-per-game-profile)).

## Tribes: Vengeance (1 November)

Vengeance's menu follows the same order and handlers (This Match, Play Online,
Options, Help, Choose Game), built from its own keys in `DefUser.ini`
([mobile_controls.m](../../port/apple/mobile_controls.m), `tb_vengeance`).

| Section | Vengeance | Same as T1/T2? |
| --- | --- | --- |
| This Match: game menu | Game Menu in play, Back to Game in menus (Escape) | Same item and handler |
| Scores | F2 | Each game's own key |
| Chat | T all, Y team, V quick chat; keyboard opens | Same handler (`openChat`) |
| Actions | Objectives (C) and Loadout (Shift) stay held until chosen again; Use F, third person Z, zoom level R; Drop Weapon (Alt held briefly), Respawn B | Held items are new: the game reads C and Shift as held buttons |
| Campaign | Quick Save F5, Quick Load F8 | Vengeance only (single player) |
| Play Online | Multiplayer (not ready in TribesPad yet, says so), Community (tribesrevengeance.net, TribesPad Discord) | Same section; no server list or live count until multiplayer works |
| Options | Touch controls, trackpad, FPS (frames counted at the Metal present), Graphics Quality (Fast or Full Detail, applied at once through the console) with Resolution (1024x768, 1600x1200 default, 2048x1536; from the next start), Settings | Same items as Starsiege plus Graphics Quality, which only Vengeance has |
| Help | Touch Guide, Controller Guide (Vengeance layout: A jumps and skis while held, as in T1/T2), Console (Tab; the game starts with \`-console\` so it opens), GitHub, About | Same items; console key differs |
| Choose Game | Subtitle names the games in this build | Same handler; three-game wording |

Settings hides Keep Clear of the Notch, Render Scale, WASD Movement and Online
Play for Vengeance, as for Starsiege; the Display footer names the chosen
resolution. Play state comes from the game's GUI controller (menu up or not) rather
than a script export.
