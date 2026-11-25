# Baiji VR

GameCube and Wii games in VR, running standalone on the Valve Steam Frame.
It is my unofficial fork of [Baiji](https://github.com/thejerf/baijiSteamVR), the Steam Frame version of [dolphinXR](https://github.com/iChris4/dolphinXR/), which is based on [Dolphin](https://dolphin-emu.org/).

![Baiji VR](docs/images/cover.jpg)

This is my testing area for fixes and optimizations, and I plan to bring all of them upstream. New work is desktop-tested; testing on the Frame is in progress.

## Features

- Stereoscopic 3D or Immersive, switched mid-game
- A new VR menu with controls, stats, mods and settings
- Natural 3D, **Screen Curve**, Quick Save and Autosave
- Game Mode window that works with the controller
- Fixes for crashes and Path of Radiance

## 3D and immersion

- **Stereoscopic 3D or Immersive, any time**: press both bumpers, then **View** and **Menu**. **Switch to Flat** shows the game in the window.
- **Natural 3D**: at 100% **3D Depth**, distant scenery sits at infinity for your IPD.
- **Immersive**: **World Scale** sets the world's size. It draws two steps over **Internal Resolution**; **Dynamic Resolution** lowers that when the GPU is short.

## The VR menu

Hold **Menu** for 1 second. Tabs: **Main**, **Position**, **Stats**, **Controls**, **Mods**, **Settings**.

- **Global** or **Per-Game**: the same settings and profiles as the window.
- **Edit Controls**: pick a control on the drawing, press the button for it.
- Chips for a Wii game's controller: GameCube, Wii Remote, + Nunchuk, Sideways, Classic.
- **Stats**: frame rate and resolution in use, in front or on your wrist.
- **Save State**, **Load State** and **Autosave**. Messages show in the screen's corner.
- **Screen Curve** bends the screen. Face buttons are by position.

<img src="docs/images/menu-main.png" width="48%" alt="Main page"> <img src="docs/images/controls.png" width="48%" alt="Controls">

## Mods, patches, cheats and textures

In the **Mods** tab and the window's **Mods** page:

- **Mods and Cheats**: 37 cheats, patches and game settings, and 3 mods, for 23 games
- **Textures**: 24 texture packs, from their creators' pages. None is hosted here.

<img src="docs/images/menu-mods.png" width="48%" alt="Mods and Cheats">

## The window in Game Mode

Fits the panel, with **Window Scale** for the text. Left stick moves, **A** twice starts a game, **B** goes back. It docks in SteamVR's theater during a VR game.

## Game-specific fixes

- **Fire Emblem: Path of Radiance**: the freeze and white screen, confirmed on the Frame.
- **Immersive**: nothing goes missing after a multiview pass.
- **VR states** keep both eyes when loaded.
- **Video mode changes**: the screen keeps one shape.
- **All VR games**: no crash on exit, Shake works, errors show in the headset. Mods and textures download.

## Experimental

Off by default, in the **VR** settings under **Advanced**: **Dynamic Resolution**, **Pin Emulation Threads**, **Performance Hint** and [more](docs/SETTINGS.md#performance), with a [performance test](docs/USER_GUIDE.md#performance-test).

## Requirements

- A Steam Frame with Developer Mode, SSH and about 250 MB free
- A Linux PC (x86-64) with Git, Podman and Flatpak
- Your own GameCube and Wii games

## Install

This fork has no download yet; [upstream Baiji](https://github.com/thejerf/baijiSteamVR/releases) has one.

1. Set up the [SSH name `frame`](docs/USER_GUIDE.md#connect-to-the-frame) on your PC.

2. Get the repository, build it and install it:

   ```sh
   git clone --recurse-submodules https://github.com/simplysanatori/baijiSteamVR.git
   cd baijiSteamVR
   ./scripts/compile-cross-sdk.sh
   ./scripts/deploy-frame.sh frame
   ```

3. Add it to Steam:

   ```sh
   ssh frame "steamos-add-to-steam ./baiji/bin/baiji-vr"
   ```

4. Launch **baiji-vr** from your Steam library.

## First run

Add your games' folder in the settings, under **Paths**, then double-click a game.

## Controls

| Steam Frame | What it does |
|---|---|
| Hold **Menu** for 1 second | VR menu |
| **View**, then right or left bumper | Quick Save or Quick Load (hold the left bumper) |
| **View**, then right trigger | Fast Forward on or off |
| Both bumpers, then **View** and **Menu** | Stereoscopic 3D or Immersive |
| Grip | Move the screen |

Full list: [docs/CONTROLS.md](docs/CONTROLS.md).

## Update

Quit the emulator, then in the `baijiSteamVR` folder:

```sh
git pull
git submodule update --init --recursive
./scripts/compile-cross-sdk.sh
./scripts/deploy-frame.sh frame
```

## Uninstall

Run this on the Frame. `--purge` also deletes your settings and saves; moving saves to Dolphin is in the [user guide](docs/USER_GUIDE.md#uninstall).

```sh
~/baiji/bin/baiji-uninstall
```


## Troubleshooting

- The pointer is off in Stereoscopic 3D: recenter once.

More: [docs/USER_GUIDE.md](docs/USER_GUIDE.md#troubleshooting).

## Reporting a problem

Turn on the detailed log, then play until the problem happens:

```sh
ssh frame "touch ~/.config/baiji/debug-logging"
```

The logs are in `~/.local/share/baiji/Logs`; check them before sharing. Don't report this fork's problems to Dolphin or Baiji.

## More

- [User guide](docs/USER_GUIDE.md)
- [AGENTS.md](AGENTS.md): for developers and AI agents
- [Building from source](docs/build-runbook.md)
- [Changelog](docs/CHANGELOG.md)
- [Baiji's README](README.upstream.md)

## Support

If you want to support this app or my other Steam Frame ports, such as PrimedFrame, you can [buy me a coffee](https://ko-fi.com/sana748653).

## Credits

- [Dolphin](https://dolphin-emu.org/): the emulator
- [dolphinXR](https://github.com/iChris4/dolphinXR/) by iChris4: the VR modes, and some performance switches
- [Baiji](https://github.com/thejerf/baijiSteamVR) by thejerf: the Steam Frame version
- [Azahar for Frame](https://github.com/simplysanatori/AzaharVR), my Azahar fork: the VR menu, Dynamic Resolution, window fitting and downloads
- [GameTDB](https://www.gametdb.com/) and [libretro-thumbnails](https://github.com/libretro-thumbnails): box art
- Mods, cheats and texture packs: their authors, named in the app
- Valve: SteamVR and the Frame

Full list: [Externals/licenses.md](Externals/licenses.md). Not affiliated with Nintendo, Valve or the Dolphin project.

## Licence

GPLv3 or any later version. Dolphin's own files are GPLv2 or later. See [COPYING](COPYING) and [LICENSES](LICENSES).

## How this was made

I started this project and used AI (Claude) to speed up development. I write, review and fix code by hand all the way through, keep the AI's work checked and reined in, and aim to keep the port as lightweight as possible. I test it on my own Steam Frame.
