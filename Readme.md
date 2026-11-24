# Baiji VR

A personal fork of Baiji for playing GameCube and Wii games on the Steam Frame.

Based on [Baiji](https://github.com/thejerf/baijiSteamVR), a Steam Frame version of [dolphinXR](https://github.com/iChris4/dolphinXR/), which is based on [Dolphin](https://dolphin-emu.org/).

This is a testing area for fixes and optimizations. The plan is to bring all of them upstream. You are welcome to use it; expect rough edges. It is not official, so please don't report its problems to Dolphin.

## Features

- A menu in the headset, with pointer beams
- **Stereoscopic 3D** and **Immersive**, with a switch during the game
- Move and resize the screen with the grips
- Quick Save, Quick Load, save states with pictures, and Autosave
- Mods, patches, cheats and texture packs for many games
- Box art, a dark style, and windows that fit Game Mode
- Experimental performance switches, such as **Dynamic Resolution**

## Game fixes

- **Fire Emblem: Path of Radiance**: the freeze and white screen (desktop-tested only; the Frame is next)
- The screen keeps one shape when a game changes video modes
- A crash on exit after a VR game
- Warnings and errors show in the headset
- **Immersive** no longer locks two of your settings
- Wii Remote extensions can be chosen without the mapping window

## How it works

- The game runs on the Frame itself; nothing is streamed from a PC.
- **Stereoscopic 3D** is a big 3D screen. **Immersive** puts you inside the game.

The new work is tested on a desktop with a simulated headset. Headset testing is in progress.

## Requirements

- A Steam Frame with Developer Mode and SSH on
- A Linux PC (x86_64) with Git and Podman, and Flatpak for the first build
- Your own legally dumped games

## Install

This fork has no download yet. For one, use [upstream Baiji](https://github.com/thejerf/baijiSteamVR/releases). To build this fork:

1. [Set up the SSH host `frame`](docs/USER_GUIDE.md#connect-to-the-frame) on your PC.
2. Build and install:

   ```bash
   git clone --recurse-submodules https://github.com/simplysanatori/baijiSteamVR.git
   cd baijiSteamVR
   ./scripts/compile-cross-sdk.sh
   ./scripts/deploy-frame.sh frame
   ```

3. Add it to Steam, the first time only:

   ```bash
   ssh frame "steamos-add-to-steam ./baiji/bin/baiji-vr"
   ```

## First run

1. Start `baiji-vr` from your Non-Steam games.
2. Add your games folder in the settings, under **Paths**.
3. Choose **Display** in the settings, under **VR**.
4. Start a game. Hold **Menu** for the VR menu.

## Controls

| Action | Press |
| --- | --- |
| VR menu | Hold **Menu** for 1 second |
| Close game | Hold **View** for 1.5 seconds |
| Quick Save | **View**, then Right Bumper |
| Quick Load | **View**, then hold Left Bumper |
| Fast Forward | **View**, then Right Trigger |
| Reset screen or view | Hold Right Stick Click |
| Stereoscopic 3D or Immersive | Both Bumpers, then **View** and **Menu** |
| Move screen | Grip |

More in the [user guide](docs/USER_GUIDE.md#controls).

## Update

Close Baiji, then run this in the `baijiSteamVR` folder on your PC:

```bash
git pull
git submodule update --init --recursive
./scripts/compile-cross-sdk.sh
./scripts/deploy-frame.sh frame
```

## Uninstall

Close Baiji, then run this in a Desktop terminal on the Frame:

```bash
~/baiji/bin/baiji-uninstall
```

Remove the `baiji-vr` shortcut from Steam by hand. Moving saves to Dolphin: [user guide](docs/USER_GUIDE.md#uninstall-or-move-to-dolphin).

## Troubleshooting

- **The pointer is off in Stereoscopic 3D:** recenter once.
- **A game looks wrong in 3D:** try the other **Display**, or lower **3D Depth**.
- **Fire Emblem: Path of Radiance still goes white:** turn off **Enable Dual Core** for that game.
- **Sound comes from the window's place:** a SteamOS limit, with no fix yet.

More in the [user guide](docs/USER_GUIDE.md#troubleshooting).

## Reporting a problem

Turn on the detailed log, then play until the problem happens:

```bash
ssh frame "touch ~/.config/baiji/debug-logging"
```

The logs are in `~/.local/share/baiji/Logs/`. Check them before you share them. Issues are not open on this fork yet.

## More

- [User guide](docs/USER_GUIDE.md)
- [AGENTS.md](AGENTS.md), for developers and AI agents
- [Building](docs/build-runbook.md)
- [Baiji's own README](docs/UPSTREAM-README.md), with its changelog

## Support

- dolphinXR: <https://github.com/sponsors/iChris4>
- This fork: <https://ko-fi.com/sana748653>

## Credits

- [Dolphin](https://dolphin-emu.org/): the emulator
- [dolphinXR](https://github.com/iChris4/dolphinXR/): the VR version, and some of the performance work
- [Baiji](https://github.com/thejerf/baijiSteamVR): the Steam Frame version
- Azahar for Frame: the VR menu, Dynamic Resolution, window fitting and mod downloads
- [GameTDB](https://www.gametdb.com/) and [libretro-thumbnails](https://github.com/libretro-thumbnails): box art

## Licence

GPL-3.0-or-later as a whole; see [COPYING](COPYING) and [LICENSES](LICENSES).

## How this was made

I started this project and used AI (Claude) to speed up development. I write, review and fix code by hand all the way through, keep the AI's work checked and reined in, and aim to keep the port as lightweight as possible. I test it on my own Steam Frame.
