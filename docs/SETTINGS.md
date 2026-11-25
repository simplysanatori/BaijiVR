# Settings

These are the **VR** settings. Everything else is Dolphin's own. The VR menu uses the same names: its **Position** tab has Display and Screen, its **Settings** tab has **General**, **Graphics** and **Immersive**, each for every game (**Global**) or for the running one (**Per-Game**).

## Display

| Setting | What it does |
|---|---|
| **Stereoscopic 3D** | The game in 3D on a screen |
| **Immersive** | You are inside the game. Many games need their own settings to look right |
| **Flat** | A normal window, without VR |

## Screen

- **Distance** and **Size**: where the screen is and how tall it is.
- **Screen Curve**: bends the screen around you, from Off to 90° in steps of 15°. Greyed where the VR runtime cannot curve a screen.
- **Aspect Ratio**: Auto, 16:9 or 4:3.
- **3D Depth**: how deep the picture is. Natural follows the distance between your eyes and the screen's size.
- **World Scale**: higher makes the world smaller, as Immersive's does.
- **Lock Screen**: keeps the grips from moving the screen.
- **2D Parts on a Screen** and **HUD Depth**: for Immersive.

## Immersive

- **World Scale**: game units per meter. Higher makes the world smaller.
- **3D Strength**: how deep the world looks. 100% is natural.
- **Lean Back**, **Camera Forward** and **Camera Height**: where the camera is.
- **Camera Anchor** and **Controller Anchor**: only in games with an anchor set up.
- **Remove Cinematic Bars**: removes the black bars at the top and bottom.

## Headset

- **Refresh Rate**: a multiple of the game's frame rate is smoothest, for example 120 Hz for 60 FPS.
- **Resolution Scale**: higher is sharper and slower.
- **Frame Pacing**: Every Refresh shows a picture at each refresh of the headset. Match Game shows one for each frame of the game, and lets SteamVR smooth the motion.
- **Passthrough**, **Gamma**, **Foveated Rendering** and **Dynamic Foveation**.

## Menu and Pointers

- **Pointer Beams**: shows where the controllers point during a game. Off by default.
- **Messages**: short messages in the headset.
- **Show Stats**: the frame rate, the resolution in use and the headset's numbers while you play. Off by default.
- **Stats Position**: **In Front**, a window in the room that a grip moves, or **On Wrist**, on the left controller.

## Stats

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

## Autosave

| Setting | Default |
|---|---|
| **Autosave** | off |
| **Autosave Every** | 5 minutes |
| **Autosaves to Keep** | 3 |

## Performance

Under **Advanced**. All are off by default.

| Setting | What it does | Where |
|---|---|---|
| **Dynamic Resolution** | Stereoscopic 3D: up to two steps over **Internal Resolution** while the GPU has room. Immersive is drawn two steps over it (no more than fills the eye image); this steps it down, to the setting at the least, while the GPU is the limit | both |
| **Pin Emulation Threads** | Keeps the main emulation threads on the fastest CPU cores. From the next game start | both |
| **Performance Hint** | Asks the VR runtime for high CPU and GPU speed. From the next game start | both |
| **Non-Blocking EFB Copies** | Does not wait for the GPU when the game copies the picture to memory | both |
| **Non-Blocking Perf Queries** | Does not wait for the GPU's answers about what was drawn | both |
| **Skip Overwritten Clears** | Leaves out clears that are drawn over anyway | Immersive |
| **Performance Log** | Writes a line a second to `perf.csv` in the Logs folder | both |

The rest of **Advanced** says what it does when you point at it.
