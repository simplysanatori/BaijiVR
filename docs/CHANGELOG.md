# Release notes

Baiji's own changelog is in [its README](../README.upstream.md). Thanks to thejerf for Baiji and to iChris4 for dolphinXR, which Baiji VR is built on.

## Baiji VR, first release (not out yet, November 2026)

### Highlights

- A completely redesigned VR menu, with your games, save states, stats, controls, mods and settings
- Start another GameCube or Wii game without leaving VR
- Stereoscopic 3D, Immersive and Flat, switched during a game
- Natural 3D for your IPD, and a curved screen
- **Edit Controls** on drawings of the controllers
- Mods, cheats and texture packs in the app
- Fire Emblem: Path of Radiance no longer freezes

### VR menu

- **Main**, **Games**, **Position**, **Stats**, **Controls**, **Mods** and **Settings**.
- **Global** and **Per-Game** settings, the same ones as in the window.
- Quick Save, Quick Load, save states with pictures, and Autosave.
- **Show Stats**: frame rate, the resolution in use and Dynamic Resolution's changes, in front of you or on the wrist.
- Pointer beams, and short messages in the headset. In Stereoscopic 3D, the answer to a press is in the screen's corner.

### Games

- The **Games** tab: covers or banners, GameCube and Wii chips, search, sorting and favourites.
- Choosing a game closes the running one, after a question, and starts in VR.
- **Add Folder** adds games to the list.
- **Download Thumbnails** and **Download Game Covers**: covers from libretro-thumbnails on GitHub. Nothing is downloaded until you choose it.
- **Choose Another Cover**, **Use My Own Image**, **No Cover** and **Reset Cover** for one game.

### Display

- The modes are called **Stereoscopic 3D** and **Immersive**. Both bumpers, then **View** and **Menu**, switch.
- **Switch to Flat** and back, during a game.
- Natural 3D: **3D Depth** follows your IPD and the screen's size.
- **Screen Curve**, up to 90°. The grips move and resize the screen.
- The screen keeps one shape when a game changes video modes.
- Immersive is sharper: drawn two steps over **Internal Resolution**, up to the eye image. **Dynamic Resolution** steps it down while the GPU is the limit.
- The two screen options of Immersive are defaults and no longer locked.

### Controls

- Line drawings of your controller with the game's buttons, under the Steam Frame controllers' own names.
- **Edit Controls**: choose a control on the drawing, then press the button for it.
- A chip for each controller of a Wii game, the Classic Controller too, each with its own mapping.
- Well-known games start with the right controller, and a notice says which.
- Mappings for every game or for one game, and the saved profiles of the mapping window in the VR menu. A game's profile stays that game's.
- Stop the game, Fast Forward, Quick Save and Quick Load from the controllers.
- The Wii Remote's extension can be chosen without the mapping window.

### Mods, cheats and textures

- 37 cheats, patches and game settings, 3 mods and 24 texture packs, in the VR menu and in a game's properties.

### Windows

- One page for the VR settings, in plain words, and a dark style.
- In Game Mode: **Window Scale**, windows that fit the screen, and the controller works in them.
- In SteamVR's theater at the start and after a VR game.
- A roomier Grid View.
- **3D Depth** on a game's own VR page, which saves only what is changed on it.
- `baiji-uninstall`, which can also copy your saves to Dolphin.

### Game-specific fixes

- Fire Emblem: Path of Radiance (USA): the freeze and white screen. Confirmed on the Frame.
- Fixes to switch on under **Mods**: Eternal Darkness, F-Zero GX, Luigi's Mansion, Metroid Prime, Star Fox Adventures, The Last Story, The Legend of Zelda: Skyward Sword and Wii Sports Resort.
- 16:9 widescreen in 12 games, and 60 FPS in Super Mario Sunshine and Pikmin 2.

### Fixes

- A freeze when a paused game saves a state, as in **Switch to Flat** (two causes).
- A save state loaded in VR puts each eye's picture back in its own eye.
- No black frame after switching the display or loading a state of the other display.
- Immersive: every draw has its state bound (multiview passes left it undefined).
- Switching between VR and Flat makes no autosave.
- A crash on exit after a VR game.
- Settings changed in the VR menu are saved at once.
- A Wii Remote's Shake works in VR.
- Warnings and internal errors show in the headset.
- Mods and texture packs download on the Frame (the system's CA certificates).

### Experimental, off by default

- **Dynamic Resolution**, **Pin Emulation Threads**, **Performance Hint**, **Non-Blocking EFB Copies**, **Non-Blocking Perf Queries** and **Skip Overwritten Clears**.
- **Performance Log**, and a performance test.
