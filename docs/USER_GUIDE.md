# Baiji VR: user guide

The short version is in the [README](../Readme.md). This guide has the rest: installing and removing in detail, your games, mods and texture packs, performance, troubleshooting and what a bug report holds. The controls are in [CONTROLS.md](CONTROLS.md) and the settings in [SETTINGS.md](SETTINGS.md). Baiji's FAQ and the command line options of `baiji` and `baiji-tool` are in [Baiji's own README](../README.upstream.md).

## Status and known issues

There is no release of this fork yet. The changes are listed in [CHANGELOG.md](CHANGELOG.md).

Known issues:

- The new work is tested on a desktop with a simulated headset. Testing on the Frame is in progress.
- Fire Emblem: Path of Radiance: the watchdog patch is for the US version only.
- SteamOS places the sound at Baiji's window while it is visible. There is no per-app switch.

## Requirements

- A Steam Frame with Developer Mode and SSH, and about 250 MB free, plus your games
- A Linux PC (x86-64) with Git and Podman. Flatpak is needed for the first build
- Your own GameCube and Wii games, dumped from your own discs

## Install, update and uninstall

### Connect to the Frame

Turn on Developer Mode and SSH on the Frame. Valve's steps: <https://partner.steamgames.com/doc/steamhardware/steamframe/setup>. Then add this to `~/.ssh/config` on your PC, with your Frame's address:

```sshconfig
Host frame
    HostName 192.168.1.123
    User steamos
```

Check it with `ssh frame`. With another name, give that name to `deploy-frame.sh`.

### Install

The README has the steps. The finished files are in `state/stage-frame/home/steamos/baiji` on your PC, and in `~/baiji` on the Frame. Add debug symbols with `DEBUG_SYMBOLS=1 ./scripts/compile-cross-sdk.sh`. Build notes, a release tarball and smoke tests are in [build-runbook.md](build-runbook.md).

### Update

Quit the emulator, then run this in the `baijiSteamVR` folder on your PC:

```sh
git pull
git submodule update --init --recursive
./scripts/compile-cross-sdk.sh
./scripts/deploy-frame.sh frame
```

### Uninstall

Quit the emulator, then run `~/baiji/bin/baiji-uninstall` in a Desktop terminal. It removes the program folder and keeps your saves and settings.

- `--migrate` copies your GameCube memory cards and Wii saves into Dolphin's user folder: the Flatpak Dolphin's if it has been run once, otherwise `~/.local/share/dolphin-emu`, or a folder you name after it. Files already there are never overwritten. Save states are not copied; they rarely load in another Dolphin build.
- `--purge` also deletes Baiji's saves, settings and cache.
- `--dry-run` shows what would happen without changing anything.
- `--yes` does not ask for confirmation.

Remove the `baiji-vr` shortcut from your Steam library by hand.

## First run

1. Add the folder with your games in the settings, under **Paths**. **File** > **Open** plays a single game.
2. Choose **Display** in the settings, under **VR**: **Stereoscopic 3D**, **Immersive** or **Flat**.
3. Double-click a game.

### Covers

The covers in the VR menu's Games tab come from libretro-thumbnails on GitHub. Nothing is downloaded until you say so: when you add a directory under **Paths**, or a refresh finds a new game, and the games lack covers, a question offers **Download Thumbnails?** (**Download** or **Not Now**). It is not asked during a game. **Download Thumbnails** in a game's menu on the Games tab does the same at any time. To fetch covers for every game in the list by itself, turn on **Download Game Covers** under **Interface** in the settings. A cover you put beside a game (`Game.cover.png` or `cover.png`) and covers already downloaded are used either way.

### Banners

The list and **View** > **Grid View** show the games' own banners. A Wii game has one once it has a save.

### Mods and texture packs

They are in the **Mods** tab of the VR menu, and on the **Mods** page of a game's properties. The list has 37 cheats, patches and game settings, 3 mods, and 24 texture packs. Some can be downloaded on the page; the others open their own page. Changes are for the game's next start.

## The VR menu

Hold **Menu** for 1 second. The rows of each tab are in [CONTROLS.md](CONTROLS.md#the-vr-menu).

- **Main**: Continue, Quick Save, Quick Load, Save State, Load State, Screenshot, Restart, Fast Forward (**Wii Home** in a Wii game), Close Game, **Switch to Flat**
- **Games**: the games of the window's list. Choose one to play it instead; it asks first
- **Position**: Reset Screen or Reset View, Display, Distance, Size, **Screen Curve**, Aspect Ratio, Lock Screen, **2D Parts on a Screen**
- **Stats**: **Show Stats** and **Stats Position**
- **Controls**: your controller with the game's buttons, **Edit Controls** and **Shortcuts**
- **Mods**: **Mods and Cheats** and **Textures**
- **Settings**: **General**, **Graphics** and **Immersive**: Internal Resolution, Frame Pacing, Autosave, Pointer Beams, Messages, **World Scale** and more

**Global** and **Per-Game** at the top right say whose settings the page shows: every game's, or only this game's. The settings and the saved profiles are the same as in the window. The three dots have **Use Global Values for All**.

### Position and 3D

- **3D Depth** is a percentage of natural 3D. It follows your IPD and the screen's size; at 100% distant scenery sits at infinity.
- **Screen Curve** goes from Off to 90° in steps of 15°. It is greyed where the VR runtime cannot curve a screen.
- **World Scale** (Immersive): higher makes the world smaller.

### Immersive resolution

Immersive draws two steps over **Internal Resolution**, up to the size that covers the eye image. **Dynamic Resolution** (experimental) steps it down to the setting while the GPU is the limit.

### Stats

**Show Stats** keeps the numbers in view. **Stats Position** is **In Front**, a window you can move with a grip, or **On Wrist**, on the left controller while the back of that hand faces you. **Internal Resolution** shows the one in use. With **Dynamic Resolution** on, the row lights for a few seconds when it changes, and **Last Change** says from what to what. The **Stats** tab shows the last second with **Show Stats** off, too.

### Edit Controls

The **Controls** tab has a chip for each controller of a Wii game: **GameCube**, **Wii Remote**, **+ Nunchuk**, **Sideways** and **Classic**, each with its own mapping. Well-known games start with the right one and a message says which. **Edit Controls** shows the drawing: choose a control, then press the Frame button for it. **Profile** lists the mapping window's saved profiles. With **Per-Game**, the game gets its own copy. Face buttons are by position: the Frame's **B** is A, **A** is B, **Y** is X and **X** is Y. Tables are in [CONTROLS.md](CONTROLS.md#editing-the-controls).

### Messages

Short messages show in the headset. In Stereoscopic 3D the answer to a press is in the screen's corner. Warnings and internal errors show there, not in a dialog.

## Save states

**Quick Save** and **Quick Load** are on **View** and a bumper. **Save State** and **Load State** in the VR menu use slots with pictures. [Autosave](#autosave) keeps the last few.

## The window in Game Mode

Every window fits the panel. **Window Scale** in the VR settings sets the text size: 100, 150 or 200 percent, read at the start. The left stick or D-Pad moves, **A** twice starts a game, **B** goes back, the right stick scrolls. The window sits in SteamVR's theater at the start and after a VR game, and leaves it before one, which takes a few seconds (`TheaterDock`, see [SETTINGS.md](SETTINGS.md#the-window-in-steamvr)). Mods and texture packs download on the Frame.

## Fixes

- Fire Emblem: Path of Radiance: the freeze and the white screen. The GPU thread no longer races a FIFO reset or a stop of the reads, and the game's own GPU watchdog is off (US version). Confirmed on the Frame.
- Immersive: every draw has its state bound. Vulkan's validation layer found draws without it.
- A paused game no longer freezes while it saves a state.
- A save state loaded in VR puts each eye's picture back in its own eye, and there is no black frame after switching the display.
- The screen keeps one shape when a game changes video modes.
- No crash on exit after a VR game.
- A Wii Remote's Shake works in VR.
- Downloads work on the Frame (the system's CA certificates).

## VR and Flat

**Switch to Flat** on the **Main** page of the VR menu shows the game in the window. The **VR** button in the menu bar goes back, and so does holding **Menu** for 3 seconds (**A** is Yes, **B** is No). The next start uses the mode you switched to last.

The game starts again in the other mode and goes on where it was, which takes a few seconds. Memory cards and Wii saves stay as they are.

- Flat is for every game, also one with a **Display** of its own. Back in VR each game has its own again.
- It does not work in RetroAchievements' Hardcore Mode, during NetPlay or with a recording.
- If the game cannot go on where it was, its state is kept in `StateSaves` as `<game ID>.mode-switch.sav`: **Emulation** > **Load State** > **Load State from File**.

## Autosave

It saves every few minutes of play, when the VR menu opens and when you close the game. Load one in the VR menu. It is off by default.

## Performance

All of the performance switches are off by default, and experimental: see [SETTINGS.md](SETTINGS.md#performance). **Non-Blocking EFB Copies**, **Non-Blocking Perf Queries** and **Skip Overwritten Clears** do nothing in many games, such as Mario Kart: Double Dash!!, Kirby's Return to Dream Land and Fire Emblem: Path of Radiance.

### Performance test

Copy [perf-test.example.txt](perf-test.example.txt) to `~/.config/baiji/perf-test.txt` on the Frame and start a game. It goes through the settings in turns and puts them back. The results are in `~/.local/share/baiji/Logs/perf-test-<date-time>.txt`.

## Files

| What | Where on the Frame |
|---|---|
| Program | `~/baiji` |
| Settings | `~/.config/baiji` |
| Saves, save states and logs | `~/.local/share/baiji` |
| Cache | `~/.cache/baiji` |

## Troubleshooting

- The pointer is off in Stereoscopic 3D: recenter once.
- A 2D part looks wrong in Immersive: turn off **2D Parts on a Screen**. Or try **Exact 2D Depth** or **Fix Cut-Off 2D Parts** under **Advanced**.
- Objects are missing in Immersive: turn on **Show Missing Objects**.
- One eye looks wrong: turn off **Draw Both Eyes at Once**.
- **Display** cannot be changed in the headset: turn off **Foveated Rendering**.
- A game's effects look wrong: turn off **Non-Blocking EFB Copies**.
- A game is too slow: lower **Internal Resolution**. Some GameCube games are heavy for the Frame.
- Sound comes from the window's place: a SteamOS limit.

## Reporting a problem

Turn on the detailed log with `ssh frame "touch ~/.config/baiji/debug-logging"`, play until the problem happens, then delete that file. A report holds:

1. The game and its region, and the **Display** you used.
2. What you did, what happened, and what you expected.
3. The logs from `~/.local/share/baiji/Logs`. Check them before you share them.

Please don't report problems of this fork to Dolphin or Baiji.

## Support upstream

dolphinXR: <https://github.com/sponsors/iChris4>
