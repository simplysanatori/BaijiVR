# Controls

![What each Frame button does in a game](images/controls.png)

## In every game

| Steam Frame | What it does | Notes |
|---|---|---|
| **Menu**, held for 1 second | VR menu | A short press is the game's button |
| **View**, held for 1.5 seconds | Stop the game | Also in the VR menu. A short press is the game's button |
| **View**, then right bumper | Quick Save | When you let go of the bumper |
| **View**, then left bumper held for half a second | Quick Load | |
| **View**, then right trigger | Fast Forward on or off | When you let go of the trigger |
| Both bumpers, then **View** and **Menu** | Stereoscopic 3D or Immersive | |
| Right stick click, held for 1 second | Reset the screen or view | |
| Grip | Move the screen | **Lock Screen** turns it off |
| Grip and stick | Screen distance and size | |
| Grip and D-Pad left or right | Nudge the screen | |

In Flat, **Menu** held for 3 seconds asks **Switch to VR?**: **A** is Yes, **B** is No. The game gets **Menu**, too.

The **Controls** tab of the VR menu shows your controller with the game's buttons. Its **Shortcuts** page has the table above.

## Face Buttons

They are by position, as on Nintendo's controllers.

| Steam Frame | GameCube | Wii Remote | Wii Remote (Sideways) | Classic Controller |
|---|---|---|---|---|
| **A** (bottom) | B | B | 2 | B |
| **B** (right) | A | A | A | A |
| **X** (left) | Y | 2 | 1 | Y |
| **Y** (top) | X | 1 | B | X |

The right trigger is the Wii Remote's B, too, and the right bumper shakes it. The Classic Controller's L and R are the triggers, ZL and ZR the bumpers, **View** is − and **Menu** is +.

## Wii Games

The **Controls** tab has the controller of a Wii game: **GameCube Controller**, **Wii Remote**, **Wii Remote + Nunchuk**, **Wii Remote (Sideways)** or **Classic Controller**. It is for this game only and can be changed while it runs.

| | |
|---|---|
| Without a choice | The Wii Remote of the Controllers settings: Wii Remote + Nunchuk at first. |
| Well-known games | Start with their own, and a notice says which: Wii Remote (Sideways) in *Kirby's Return to Dream Land* and *New Super Mario Bros. Wii*, for example. The list is `Data/Sys/WiiControllers.ini`. Your choice is in front of it. |
| A game's own settings | A profile, the first port or the Wii Remote's source in its INI is in front of the list. A profile is in front of your choice, too. |

With the Wii Remote on its side, rolling the right controller tilts it, as turning a wheel.

## Editing the Controls

**Edit Controls** on the **Controls** tab shows the game's controller with a chip on every button and stick.

| | |
|---|---|
| A chip | Listens: **Press a Button…** shows. Press the Frame button for it. |
| A stick's chip | **Push the Stick Up…**, then **Push the Stick Right…**. |
| **Cancel** | Stops listening. |
| **Reset to Default** | The Steam Frame layout of this controller. |
| **Global** | The mapping of this controller in every game. |
| **Per-Game** | This game gets its own copy of the mapping. **Use Global Values** removes it. |

Each controller has its own mapping: GameCube Controller, Wii Remote, Wii Remote + Nunchuk, Wii Remote (Sideways) and Classic Controller. The mapping window in the Controllers settings shows the one the game has, and its title says which.

| Mapping | File in `Config/` |
|---|---|
| Every game | `GCPadNew.ini`, `WiimoteNew.ini` |
| A Wii Remote that `WiimoteNew.ini` is not for | `Profiles/Wiimote/VR <controller>.ini` |
| One game | `Profiles/GCPad/VR GameCube Controller - <game ID>.ini`, `Profiles/Wiimote/VR <controller> - <game ID>.ini` |

The Wii Remote's extension can be chosen in the Controllers settings without opening the mapping window. During a Wii game, that changes the game's controller.
