# Baiji VR

A personal fork of [Baiji](https://github.com/thejerf/baijiSteamVR) with fixes and optimizations for playing GameCube and Wii games on the Steam Frame.

Baiji is a Steam Frame cut of [dolphinXR](https://github.com/iChris4/dolphinXR/), a VR fork of [Dolphin](https://dolphin-emu.org). This fork's home is [simplysanatori/baijiSteamVR](https://github.com/simplysanatori/baijiSteamVR).

## What this is

This is a testing area. New work lands here first, and the plan is to bring all of it upstream.

If you want to use it anyway, you are welcome to. Expect rough edges.

It is not official, and it is not affiliated with Dolphin or Baiji's author. Please do not report problems with this fork to the Dolphin project.

## How well tested is it?

The new work is tested on a desktop with a simulated headset. Testing on a real Steam Frame is in progress. Nothing here comes with performance numbers from the Frame.

## Extra features

- In-headset VR menu with tabs for **Main**, **Screen**, **Controls**, **Mods** and **Settings**
- Pointer beams, short on-screen messages, and moving or resizing the screen with the grips
- Quick Save, Quick Load, and save states with pictures
- Autosave
- Controller diagrams that show your own bindings
- **Stereoscopic 3D** and **Immersive**, with a shortcut to switch mid-game
- **3D Depth** that follows the distance between your eyes
- A **Mods** page for mods, patches, cheats and texture packs
- Cover art for GameCube and Wii games, in the list and in Grid View
- A dark style, and windows that fit the screen in Game Mode
- The Frame controllers' own button names

## The VR menu

Hold **Menu** for one second. Everything below is also on the **Shortcuts** page of the **Controls** tab.

| Do this | Press |
| --- | --- |
| Close Game | Hold **View** for 1.5 seconds |
| Quick Save | **View**, then Right Bumper |
| Quick Load | **View**, then hold Left Bumper |
| Fast Forward | **View**, then Right Trigger |
| Reset Screen or View | Hold Right Stick Click |
| Stereoscopic 3D or Immersive | Both Bumpers, then **View** and **Menu** |
| Move Screen | Grip |
| Screen Distance and Size | Grip and Stick |
| Nudge Screen | Grip and D-Pad Left or Right |

## Mods

The **Mods** tab, and the Mods page in the desktop window, list 41 mods, patches and cheats and 23 texture packs. Texture packs are downloaded from the page. Changes apply at the game's next start.

## Performance switches

All of these are off by default. Several only help certain games. They are experimental.

- **Dynamic Resolution** goes up to two steps above Internal Resolution while the GPU has room.
- **Pin Emulation Threads** and **Performance Hint** keep the main threads on the fastest CPU cores and ask the VR runtime for high CPU and GPU speed. They apply at the next game start.
- **Non-Blocking EFB Copies**, **Non-Blocking Perf Queries** and **Skip Overwritten Clears** cut GPU waits. They do nothing in games that never trigger them.
- **Performance Log** writes a line a second to `perf.csv` in the Logs folder.

To compare settings, copy [docs/perf-test.example.txt](docs/perf-test.example.txt) to `~/.config/baiji/perf-test.txt` and start a game. It switches the settings in turns, puts them back, and writes the results to a file in the Logs folder.

## Game fixes

- **Fire Emblem: Path of Radiance** froze or showed a white screen. Three separate causes are fixed: the emulator reset its command queue while the GPU thread still ran, the game's own GPU watchdog shut it down on a slow frame, and a command could be cut off when the game stopped reading. The watchdog patch is for the US version only. Tested on the desktop, not yet confirmed on the Frame.
- Warnings and internal errors show in the headset, not in a hidden dialog.
- Crash on exit after a VR game: fixed.
- The screen keeps one shape when a game changes video modes.
- **Immersive** no longer locks two settings. Your own values win.
- In Game Mode, the game renders to the main window. On the desktop, the game list and Properties work mid-game.
- Choose the Wii Remote's extension in the Controllers pane, even mid-game.
- `baiji-uninstall` is included. See below.

## Get it

There is no release of this fork yet. To build it yourself, follow [docs/build-runbook.md](docs/build-runbook.md). For a normal release, use [upstream Baiji](https://github.com/thejerf/baijiSteamVR/releases).

Games are not included. Use your own legally dumped copies.

## Uninstall

Close Baiji, then run `~/baiji/bin/baiji-uninstall` in a Desktop terminal. It removes the program folder and keeps your saves and settings.

- `--migrate` copies your GameCube memory cards and Wii saves into Dolphin's user folder. Files already there are never overwritten. Save states are not copied.
- `--purge` also deletes Baiji's saves, settings and cache.
- `--dry-run` shows what would happen without changing anything.

Remove the `baiji-vr` shortcut from your Steam library by hand.

## Credits

- [Dolphin](https://dolphin-emu.org): the emulator underneath everything.
- [dolphinXR](https://github.com/iChris4/dolphinXR/): the VR fork Baiji is built on.
- [Baiji](https://github.com/thejerf/baijiSteamVR): the Steam Frame cut this fork is based on.
- Azahar for Frame: the in-headset menu design.

## Licence

Dolphin is GPL-2.0-or-later. Files that come from Azahar for Frame are GPL-3.0-or-later, so builds of this fork as a whole are GPL-3.0-or-later. See [COPYING](COPYING) and [LICENSES](LICENSES).
