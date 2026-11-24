# Baiji VR user guide

The short version is in the [README](../Readme.md). Command line options of `baiji` and `baiji-tool`, the FAQ and Baiji's changelog are in [Baiji's own README](UPSTREAM-README.md).

## Connect to the Frame

Turn on Developer Mode and SSH on the Frame. Valve's steps: <https://partner.steamgames.com/doc/steamhardware/steamframe/setup>. Then add this to `~/.ssh/config` on your PC, with your Frame's address:

```sshconfig
Host frame
    HostName 192.168.1.123
    User steamos
```

Check it with `ssh frame`. With another name, give that name to `deploy-frame.sh`.

## Build options

- Debug symbols: `DEBUG_SYMBOLS=1 ./scripts/compile-cross-sdk.sh`
- The finished files are in `state/stage-frame/home/steamos/baiji`.
- A release tarball, build notes and smoke tests: [build-runbook.md](build-runbook.md)

## Display

**Display** is in the settings under **VR**. During a game, change it in the VR menu.

- **Stereoscopic 3D**: the game in 3D on a screen.
- **Immersive**: you are inside the game. Many games need their own settings to look right.
- **Flat**: a normal window, without VR.

## Controls

Every game:

| Action | Press |
| --- | --- |
| VR menu | Hold **Menu** for 1 second |
| Close game (also in the VR menu) | Hold **View** for 1.5 seconds |
| Quick Save | **View**, then Right Bumper |
| Quick Load | **View**, then hold Left Bumper for half a second |
| Fast Forward | **View**, then Right Trigger |
| Reset screen or view | Hold Right Stick Click for 1 second |
| Stereoscopic 3D or Immersive | Both Bumpers, then **View** and **Menu** |
| Move screen | Grip |
| Screen distance and size | Grip and Stick |
| Nudge screen | Grip and D-Pad Left or Right |

A short press of **Menu** or **View** is the game's button. The **Controls** tab of the VR menu shows your controller with the game's buttons. Its **Shortcuts** page has the table above.

The Wii Remote's extension can be chosen in the Controllers settings without opening the mapping window.

## The VR menu

- **Main**: Continue, save states with pictures, Quick Save, Quick Load, Autosave
- **Screen**: Distance, Size, 3D Depth, Convergence, Aspect Ratio, Lock Screen, Reset Screen
- **Controls**: Game and Shortcuts
- **Mods**: Mods and Cheats, and Textures
- **Settings**: Internal Resolution, Frame Pacing, Pointer Beams, Messages and more

## Screen settings

- **3D Depth**: how deep the picture is. Natural follows the distance between your eyes and the screen's size.
- **Convergence**: how far things come out of the screen. Lower is easier on the eyes.
- **Lock Screen**: the grips do not move the screen.

## Headset settings

- **Refresh Rate**: a multiple of the game's frame rate is smoothest, for example 120 Hz for 60 FPS.
- **Resolution Scale**: sharper and slower when higher.
- **Frame Pacing**: Every Refresh shows a picture at each refresh. Match Game shows one for each frame of the game and lets SteamVR smooth the motion.
- **Passthrough**, **Gamma**, **Foveated Rendering** and **Dynamic Foveation** are here too.

## Immersive settings

**World Scale**, **3D Strength**, **Lean Back**, **Camera Forward**, **Camera Height**, **Camera Anchor**, **Controller Anchor** and **Remove Cinematic Bars**. The rest is under **Advanced**: pause on a setting to read what it does.

## Autosave

Off by default. It saves every few minutes of play, when the VR menu opens and when you close the game. **Autosave Every** is 5 minutes and **Autosaves to Keep** is 3, unless you change them. Load one in the VR menu.

## Mods

They are in the **Mods** tab of the VR menu, and on the **Mods** page of a game's properties. The list has 41 mods, patches and cheats, and 23 texture packs. Some can be downloaded on the page; others open their own page. Changes are for the game's next start.

## Box art

Covers come from GameTDB, or else libretro-thumbnails. **View** > **Grid View** shows them as a grid.

## Performance switches

All off by default, and experimental. They are under **Advanced** > **Performance** in the VR settings.

- **Dynamic Resolution**: up to two steps over **Internal Resolution** while the GPU has room. Stereoscopic 3D only.
- **Pin Emulation Threads**: the main emulation threads stay on the fastest CPU cores. From the next game start.
- **Performance Hint**: asks the VR runtime for high CPU and GPU speed. From the next game start.
- **Non-Blocking EFB Copies**: no wait for the GPU when the game copies the picture to memory.
- **Non-Blocking Perf Queries**: no wait for the GPU's answers about what was drawn.
- **Skip Overwritten Clears**: leaves out clears that are drawn over anyway. Immersive only.
- **Performance Log**: a line a second in `perf.csv` in the Logs folder.

**Non-Blocking EFB Copies**, **Non-Blocking Perf Queries** and **Skip Overwritten Clears** do nothing in many games, such as Mario Kart: Double Dash!!, Kirby's Return to Dream Land and Fire Emblem: Path of Radiance.

### Performance test

Copy [perf-test.example.txt](perf-test.example.txt) to `~/.config/baiji/perf-test.txt` on the Frame and start a game. It goes through the settings in turns and puts them back. The results are in `~/.local/share/baiji/Logs/perf-test-<date-time>.txt`.

## Uninstall or move to Dolphin

Close Baiji, then run `~/baiji/bin/baiji-uninstall` in a Desktop terminal. It removes the program folder and keeps your saves and settings.

- `--migrate` copies your GameCube memory cards and Wii saves into Dolphin's user folder: the Flatpak Dolphin's if it has been run once, otherwise `~/.local/share/dolphin-emu`, or a folder you name after it. Files already there are never overwritten. Save states are not copied; they rarely load in another Dolphin build.
- `--purge` also deletes Baiji's saves, settings and cache.
- `--dry-run` shows what would happen without changing anything.
- `--yes` does not ask for confirmation.

Remove the `baiji-vr` shortcut from your Steam library by hand.

## Folders

- Program: `~/baiji`
- Settings: `~/.config/baiji`
- Saves, states and logs: `~/.local/share/baiji`
- Cache: `~/.cache/baiji`

## Troubleshooting

- **The pointer is off in Stereoscopic 3D:** recenter once.
- **A 2D part looks wrong in Immersive:** turn off **2D Parts on a Screen**. Or try **Exact 2D Depth** or **Fix Cut-Off 2D Parts** under **Advanced**.
- **Objects are missing in Immersive:** turn on **Show Missing Objects**.
- **One eye looks wrong:** turn off **Draw Both Eyes at Once**.
- **Display cannot be changed in the headset:** turn off **Foveated Rendering**.
- **Fire Emblem: Path of Radiance goes white:** turn off **Enable Dual Core** for that game. The European and Japanese versions do not have the watchdog patch.
- **A game is too slow:** lower **Internal Resolution**. Some GameCube games are heavy for the Frame.
- **Sound comes from the window's place:** SteamOS places the sound at the window while it is visible. There is no per-app switch.

## What a bug report contains

1. The game and its region, and the **Display** you used.
2. What you did, what happened, and what you expected.
3. The logs from `~/.local/share/baiji/Logs/`, made with `~/.config/baiji/debug-logging` in place. Delete that file afterwards. Check the logs before you share them.
