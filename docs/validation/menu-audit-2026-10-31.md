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

- Choose Game's subtitle, the gear menu's **Open Tribes 2 Directly** and the
  Settings row **Game Picker at Launch** ("Off, it opens Tribes 2 directly") all
  name two games. See the [Vengeance plan](../VENGEANCE-IMPLEMENTATION-PLAN.md#three-games-in-the-picker).
- Game-specific menu sections branch on one `BOOL`. A third game should first
  move them into a per-game table (the profile in
  [LAUNCHER-PLAN.md](../LAUNCHER-PLAN.md#3-shared-controls-per-game-profile)).
