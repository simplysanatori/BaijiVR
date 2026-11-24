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

### Box art

Covers come from GameTDB, or else libretro-thumbnails. **View** > **Grid View** shows them as a grid.

### Mods and texture packs

They are in the **Mods** tab of the VR menu, and on the **Mods** page of a game's properties. The list has 41 mods, patches and cheats, and 23 texture packs. Some can be downloaded on the page; the others open their own page. Changes are for the game's next start.

## The VR menu

- **Main**: Continue, Quick Save, Quick Load, Save State, Load State, Screenshot, Reset View, Fast Forward, Restart, Close Game
- **Screen**: Distance, Size, 3D Depth, Convergence, Aspect Ratio, Lock Screen, Reset Screen
- **Controls**: your controller with the game's buttons, and the shortcuts
- **Mods**: Mods and Cheats, and Textures
- **Settings**: Internal Resolution, Frame Pacing, Autosave, Pointer Beams, Messages and more

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
- Fire Emblem: Path of Radiance goes white: turn off **Enable Dual Core** for that game.
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
