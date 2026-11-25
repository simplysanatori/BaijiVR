# Changelog

Baiji's own changelog is in [its README](../README.upstream.md).

## Not released yet (November 2026)

### VR menu and controls

- A new VR menu: **Main**, **Position**, **Stats**, **Controls**, **Mods** and **Settings**.
- **Global** and **Per-Game** settings in the menu, the same ones as in the window.
- **Show Stats**: frame rate, the resolution in use and Dynamic Resolution's changes, in front of you or on the wrist.
- **Screen Curve**.
- Pointer beams and short messages in the headset. In Stereoscopic 3D, the answer to a press is in the screen's corner.
- The grips move and resize the screen.
- Quick Save, Quick Load, save states with pictures, and Autosave.
- Stop the game, Fast Forward, and Stereoscopic 3D or Immersive, from the controllers.
- Switch between VR and Flat during a game.
- Line drawings of your controller with the game's buttons.
- The Steam Frame controllers' own names and layout.
- **Edit Controls** in the VR menu: choose a control on the drawing, then press the button for it.
- A chip for each controller of a Wii game, the Classic Controller too, with a mapping for each. Well-known games start with the right one, and a notice says which.
- Controls for every game or for one game, and the saved profiles of the mapping window in the VR menu.
- A mapping window saves where the mapping is from: a game's profile stays that game's.

### Display

- The modes are called **Stereoscopic 3D** and **Immersive**.
- Natural 3D: **3D Depth** follows your IPD and the screen's size.
- The screen keeps one shape when a game changes video modes.
- The two screen options of Immersive are defaults and no longer locked.
- Immersive is sharper: drawn two steps over **Internal Resolution**, up to the eye image. **Dynamic Resolution** steps it down while the GPU is the limit.

### Mods and games

- Mods, patches, cheats and texture packs in the VR menu and in a game's properties.
- A roomier Grid View.

### Windows

- One page for the VR settings, in plain words.
- Windows that fit the screen in Game Mode, and a dark style.
- **Window Scale** for the windows in Game Mode. The left stick or D-Pad moves, **A** twice starts a game, **B** goes back.
- In Game Mode the window sits in SteamVR's theater at the start and after a VR game.
- The Wii Remote's extension can be chosen without the mapping window.
- **3D Depth** on a game's own VR page. A game's VR page shows what the VR menu changes, and saves only what is changed on it.
- `baiji-uninstall`, which can also move your saves to Dolphin.

### Performance (experimental, off by default)

- **Dynamic Resolution**, **Pin Emulation Threads**, **Performance Hint**, **Non-Blocking EFB Copies**, **Non-Blocking Perf Queries** and **Skip Overwritten Clears**.
- **Performance Log**, and a performance test.

### Fixes

- Fire Emblem: Path of Radiance: the freeze and white screen (confirmed on the Frame).
- Immersive: every draw has its state bound (multiview passes left it undefined).
- A freeze when a paused game saves a state, as in **Switch to Flat** (two causes).
- A save state loaded in VR puts each eye's picture back in its own eye.
- No black frame after switching the display or loading a state of the other display.
- Switching between VR and Flat makes no autosave.
- A crash on exit after a VR game.
- Settings changed in the VR menu are saved at once.
- A Wii Remote's Shake works in VR.
- Warnings and internal errors show in the headset.
- Mods and texture packs download on the Frame (the system's CA certificates).
