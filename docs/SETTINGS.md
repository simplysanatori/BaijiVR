# Settings

The tabs of the VR menu, in the order of its rail. **Global** and **Per-Game** at the top right choose whose settings a page shows: every game's, or the running one's. The window's **VR** settings use the same names. Everything else in the window is Dolphin's own.

## Main

| Button | What it does |
|---|---|
| **Continue** | Back to the game |
| **Quick Save**, **Quick Load** | Saves and loads the quick slot |
| **Save State**, **Load State** | Slots with pictures, and the autosaves |
| **Screenshot**, **Restart** | |
| **Fast Forward** | In a Wii game, **Wii Home** |
| **Close Game** | Back to the window |
| **Switch to Flat** | The running game goes on in a flat window |

## Games

| Control | What it does |
|---|---|
| **All**, **GameCube**, **Wii** | Which games are listed |
| Search | Finds games as you type |
| Sort | **Recently Played**, **Favourites First**, **Title** or **Play Time** |
| **Add Folder** | Adds a folder of games to the list |
| The three dots of a game | **Play** or **Continue**, **Favourite**, **Choose Another Cover**, **No Cover**, **Use My Own Image**, **Reset Cover**, **Download Thumbnails** |

## Position

| Setting | What it does |
|---|---|
| **Reset Screen** or **Reset View** | Back in front of you |
| **Display** | **Stereoscopic 3D** or **Immersive**. **Flat** is in the window's settings |
| **Distance**, **Size** | Of the screen. In Immersive, of the one for 2D parts |
| **Screen Curve** | Off to 90°, in steps of 15°. Greyed where the VR runtime cannot curve a screen |
| **Aspect Ratio** | Auto, 16:9 or 4:3 |
| **Lock Screen** | Keeps the grips from moving the screen |
| **2D Parts on a Screen** | Immersive: menus, videos and the HUD on a screen in front of you |

## Stats

| Setting | What it does |
|---|---|
| **Show Stats** | Shows the numbers while you play. Off by default |
| **Stats Position** | **In Front**, a window in the room that a grip moves, or **On Wrist**, on the left controller |

The tab shows the last second the game ran, with **Show Stats** off too.

| Number | What it is |
|---|---|
| **Frame Rate**, **Speed** | The game's frames a second, and its speed against full speed |
| **Frame Time**, **Peak Frame Time**, **Slow Frames** | Between two frames of the game: the average, the longest, and how many took half as long again as usual. Only with **Show Stats** on |
| **Internal Resolution** | The one in use, and the setting when that is another |
| **Picture** | The size the game is drawn at |
| **Dynamic Resolution** | Off, or On with the range it moves in |
| **Last Change** | From what to what the resolution in use changed, and how long ago |
| **Headset Frame Rate**, **Late Frames** | Pictures shown a second, and refreshes that got none in time (**Every Refresh** only) |
| **GPU Time** | What a frame takes on the GPU |
| **GPU**, **CPU** | Their load, where the system says it. Only with **Show Stats** on |

The **Stats** tab of the VR menu shows the last second the game ran, with **Show Stats** off too.

## Controls

| Control | What it does |
|---|---|
| **Game**, **Shortcuts** | The game's controller, or the emulator's own buttons |
| **GameCube**, **Wii Remote**, **+ Nunchuk**, **Sideways**, **Classic** | The controller of a Wii game |
| **Edit Controls** | Changes the mapping. **Profile** switches between **Default** and the saved profiles |
| **Reset to Default** | The Steam Frame layout of this controller |

More: [CONTROLS.md](CONTROLS.md#editing-the-controls).

## Mods

| Page | What is on it |
|---|---|
| **Mods and Cheats** | Mods, cheats, patches and game settings for the game |
| **Textures** | Texture packs for the game |

Changes are for the game's next start.

## Settings

### General

| Setting | What it does |
|---|---|
| **Window Scale** | Text and buttons in the window: 100%, 150% or 200%. From the next start |
| **Passthrough** | Your room around the game, if the headset can |
| **Pointer Beams** | Where the controllers point during a game. Off by default |
| **Messages** | Short messages in the headset. In Stereoscopic 3D, the answer to a press (**Quick Saved**, **Fast Forward**) is in the screen's top right corner for 1.5 seconds |
| **Frame Pacing** | **Every Refresh**: a picture at each refresh of the headset. **Match Game**: one for each frame of the game, and SteamVR smooths the motion |
| **Refresh Rate** | A multiple of the game's frame rate is smoothest, for example 120 Hz for 60 FPS |
| **Autosave** | A state while you play and when you close the game. Off by default |
| **Every** | Minutes of play between two autosaves: 5 by default |
| **Keep** | How many: 3 by default |

### Graphics

| Setting | What it does |
|---|---|
| **3D Depth** | Stereoscopic 3D: how deep the picture is. **Natural** is set from your IPD and the screen's size |
| **World Scale** | Stereoscopic 3D: higher makes the world smaller |
| **Internal Resolution** | How sharp the game is drawn |
| **Texture Filtering**, **Anisotropic Filtering**, **Anti-Aliasing** | |
| **Shader Compilation** | Ubershaders stutter less. Exclusive and Hybrid need more of the GPU |
| **Widescreen Hack** | More at the sides in games without widescreen. Objects can be missing there |
| **Skip EFB Access from CPU** | Faster. Some effects and game logic need it off |
| **Store EFB Copies to Texture Only** | Faster. Some effects need it off |
| **Store XFB Copies to Texture Only** | Faster. Some videos and loading screens need it off |
| **Foveated Rendering** | Less detail at the edges of the view. Has to be off to change **Display** in the headset |
| **Gamma** | Darkens a picture that is too bright |

#### Performance

All are off by default. In the window they are under **Advanced**.

| Setting | What it does | Where |
|---|---|---|
| **Dynamic Resolution** | Stereoscopic 3D: up to two steps over **Internal Resolution** while the GPU has room. Immersive is drawn two steps over it (no more than fills the eye image); this steps it down, to the setting at the least, while the GPU is the limit | both |
| **Pin Emulation Threads** | Keeps the main emulation threads on the fastest CPU cores. From the next game start | both |
| **Performance Hint** | Asks the VR runtime for high CPU and GPU speed. From the next game start | both |
| **Non-Blocking EFB Copies** | Does not wait for the GPU when the game copies the picture to memory | both |
| **Non-Blocking Perf Queries** | Does not wait for the GPU's answers about what was drawn | both |
| **Skip Overwritten Clears** | Leaves out clears that are drawn over anyway | Immersive |
| **Performance Log** | Writes a line a second to `perf.csv` in the Logs folder | both |

### Immersive

| Setting | What it does |
|---|---|
| **Immersive Mode** | Immersive or Stereoscopic 3D. The same as **Display** on **Position** |
| **World Scale** | Game units per meter. Higher makes the world smaller |
| **3D Strength** | How deep the world looks. 100% is natural |
| **Lean Back**, **Camera Forward**, **Camera Height** | Where the camera is |
| **Head Tracking** | **Turn and Move** (6DoF), **Turn Only** (3DoF) or **Off** |
| **HUD Depth** | Spreads the layers of the HUD apart in depth |
| **Remove Cinematic Bars** | The black bars at the top and bottom |

## Only in the window

| Setting | What it does |
|---|---|
| **Resolution Scale** | Of the eye images: higher is sharper and slower |
| **Dynamic Foveation** | Less detail only when the headset is busy |
| **Camera Anchor**, **Controller Anchor** | Only in games with an anchor set up |
| **Advanced** | Each one says what it does when you point at it |

### Game Covers

Under **Interface**.

| Setting | Default |
|---|---|
| **Download Game Covers** | off |

On, the covers for the games in the list, and banners for those without one, are downloaded from libretro-thumbnails on GitHub. Off, covers already on your computer are used, and adding a directory under **Paths**, or a new game found by a refresh, asks **Download Thumbnails?** for those games. GitHub sees your IP address and the names of the games that are asked for.

## The Window in SteamVR

`TheaterDock` under `[Interface]` in `Dolphin.ini`, in Game Mode only:

| Value | What it does |
|---|---|
| 1 (default) | The window is put in SteamVR's theater at the start and after a VR game, and back into the dashboard before one |
| 2 | It stays in the theater when a VR game starts |
| 0 | Off |
