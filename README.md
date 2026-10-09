# TribesPad

Tribes 2 on iPad, iPhone and Mac, using the original game and TribesNEXT client.

TribesPad translates the original x86 game ahead of time into native ARM64 code
and supplies the Windows, graphics, audio and network services it expects on Apple
platforms. The aim is the original game, offline bot matches and compatibility with
the existing Tribes 2 community, with touch and controller support on mobile.

**In development.** The Mac app and full iPad Simulator app run original offline
matches with bots. iPad touch movement, aim, weapons, firing, jets and respawn
have been exercised. Performance is still around **7–8 FPS in the Simulator**, with
slow loading and input delay. Physical-iPad gameplay is unverified; there is no
playable iPad or iPhone release yet.

![Original Tribes 2 running through Metal with iPad touch controls](docs/validation/ipad-fog-touch-2026-10-09.png)

## What works

- **Original Mac game.** Menus, Retina mouse targeting and offline Katabatic
  matches with bots. Movement, aiming and disc firing have been exercised.
- **Internet server listings on Mac.** The original browser displayed 26 community
  servers and refreshed their information. Tests do not join online matches.
- **Original iPad game in Simulator.** The Metal renderer loads Katabatic with bots.
  Touch movement, aiming, weapon switching, firing, jets and respawn work.
- **Original audio data.** WAV loading and the MP3 window tables are repaired.
  Original UI/loading samples match the game archive, and music supplies nonzero
  PCM to OpenAL. Audible quality still needs validation.
- **Saved profiles.** The bundled iPad app keeps warrior and video preferences
  between launches. Its device build includes the game files it needs locally.

## iPad controls

To try an offline match, choose **LAN GAME → HOST → Katabatic**, enable two bots,
and tap **START GAME**. Loading currently takes several minutes. Once the world
appears, tap **Touch controls**, then **Fire + Aim** to spawn.

| Control | Action |
| --- | --- |
| Move pad | Move with the original ESDF bindings |
| Look pad | Drag to aim |
| Fire + Aim | Hold to fire; drag to aim while firing |
| Jet | Hold to jet; release to recover energy |
| Ski / Jump | Original jump action |
| Weapon | Switch weapon |
| Inventory | Open inventory and hide the gameplay pads so its fields can be tapped |
| Menu | Send Escape to the original game, including when the pads are hidden |
| Keyboard | Show the onscreen keyboard for a selected text field |

Movement, look, weapons, fire and jet have been exercised in the Simulator.
Skiing, simultaneous gestures and physical-device responsiveness still need validation.
The bundled app preserves its original warrior and video preferences between launches.
In inventory, tap the original **Done** button, then **Touch controls** to resume.
Use **Hide controls** when another original dialog needs direct taps. The toolbar
stays reachable while the onscreen keyboard is open.

## Controller layout

The full app maps one extended Xbox, PlayStation or MFi gamepad to the original
bindings. Native input tests and both iPad builds pass; physical-controller
and physical-iPad gameplay are still unverified.

| Control | Action | Control | Action |
| --- | --- | --- | --- |
| Left stick | Move | Right stick | Aim |
| RT | Fire | LT | Jet |
| A | Ski / jump | X | Use pack |
| LB | Grenade | RB | Next weapon |
| Y | Inventory | Menu | Escape |

Disconnecting releases the controller's actions; backgrounding releases all controls.
Touch controls remain available alongside the controller.

## Build from source

The development build needs Xcode, pinned LLVM tools, prepared game inputs and
translated images. A clean checkout is not yet a one-command build.

In the prepared workspace, boot one iPad Simulator and build the full app:

```sh
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
export SDKROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)"
.venv/bin/python scripts/build-ipad-engine.py --angle --cpu-host-checks \
  --bundle-assets --simulator-device <UDID>
```

`--angle` selects the pinned Metal renderer. `--bundle-assets` gives the Simulator
its own game files and writable profile. Add `--build-only` to preserve a running
session. For a self-contained ARM64 device build:

```sh
export SDKROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
.venv/bin/python scripts/build-ipad-engine.py --platform device --angle \
  --cpu-host-checks --build-only
```

The device app needs Apple development signing before installation; it is not an
installable release. Mac packaging, dependency preparation, signing requirements
and build validation: **[Development guide](docs/DEVELOPMENT.md)**.

## Status and limits

- Latest iPad Simulator recordings measure **6.8 FPS playing / 7.7 FPS observing**.
  Viewpoints differ across builds, so these are not a controlled speedup claim.
  Startup, input delay and sustained performance still need work.
- Distance fog is fixed. HD texture uploads are verified, but full visual fidelity
  is not accepted.
- Both iPad targets build with an optimized executable of about **104 MB**, down
  from 158.6 MB. Verified object caching speeds up rebuilds; guest-memory checks
  stay enabled.
- Physical-iPad installation, memory usage and gameplay are unverified. The full
  iPhone game has not been accepted.
- Automated multiplayer checks stop at server discovery. Online joining and
  authentication remain unverified.

Engineering detail and current evidence live in [status](docs/STATUS.md), the
[execution plan](docs/EXECUTION-PLAN.md) and [validation records](docs/validation).

## Game files and references

Game data is not included in this repository. Downloads, textures and generated
translations stay under ignored `ref/` and `generated/` directories.
[Input hashes](inputs-manifest.json) identify the selected game and TribesNEXT files;
[dependency pins](dependencies.lock.json) include the
[HaloPad / ProjectReach](https://github.com/chrissotraidis/projectreach) reference.
Free download availability does not grant redistribution rights.

See the [product target](docs/Tribes2-Online-HD-Target.md) for the intended experience.
Problems: [open an issue](https://github.com/chrissotraidis/tribespad/issues).
