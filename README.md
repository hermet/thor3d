# Thor3D — E1M1

<img width="1928" height="1107" alt="image" src="https://github.com/user-attachments/assets/d050a154-b6e1-40fa-a0bf-e16deb31c72b" />

Just for fun: a nostalgic classic FPS demo recreating Wolf3D’s E1M1 with
ThorVG vector graphics, built with Codex.

The demo uses raycasting to project walls and ThorVG Toolkit with OpenGL
acceleration to render the scene. Walls, sliding doors, the pistol, ceiling
lights, floor lighting, and the minimap are drawn with vector shapes and
gradients. No original game files or bitmap textures are needed at runtime.

## Features

- The full **64×64 E1M1 map**, with 22 doors at their original positions and orientations.
- The original player start at `(29.5, 57.5)`, facing east.
- Blue stone, gray stone, and vertical wood paneling at the corresponding map locations.
- Six fixed prison cell walls with iron bars.
- Red wall banners on the original north/south flag-wall faces, with the central emblem replaced by the ThorVG logo. East/west faces remain gray stone.
- Ten blue pots with brown plants and two stone wells (one with blue water, one empty) at their original positions, with wall occlusion and collision.
- Sliding iron doors with visible thickness. Doors open to 95%, leaving a small edge exposed, and close automatically after three seconds. They stay open while the player occupies the doorway.
- Green ceiling lamps and four gold chandeliers at their original map positions, with soft pools of light. Chandeliers replace nearby green lamps and cast warm light.
- A toggleable outline minimap showing walls, doors, and the player position.
- Movement, running, strafing, wall collision, and hitscan pistol fire.
- Animated `lolo_walk.json` billboards at the original guard spawns. They patrol and follow nearby visible players, and shrink away after three hits.

This is an exploration prototype. Enemy attacks, sound, items, and decorative objects other than the potted plants and wells are not implemented. Secret pushwalls remain fixed walls, and the exit
does not advance to another level.

## Build and run

Install ThorVG and ThorVG Toolkit with their pkg-config metadata, along with
Meson, Ninja, and a C++17 compiler. ThorVG must support the OpenGL renderer
for GL acceleration; Toolkit falls back to CPU if GL initialization fails.

```sh
meson setup build
meson compile -C build
./build/thor3d
```

The application starts in a **1920×1080** window and requests the GL renderer.

## Controls

| Input | Action |
| --- | --- |
| W / S or Up / Down | Move forward / backward |
| A / D | Strafe left / right |
| Left / Right | Turn |
| Shift | Run |
| E or Space | Open / close a nearby door |
| Tab | Toggle the outline minimap |
| R | Reset the player, doors, and characters |
| Escape | Quit |
| Left Ctrl or left mouse button | Fire the pistol (three hits defeat a character) |

## Map data and rendering

The bundled `e1m1.h` is referenced to the original shareware E1M1 map. It
contains wall materials, floor cells, door orientations, prison wall positions,
and the player start. The original temporary boundaries around the starting
sector have been removed.

Wall tiles 1–4 use gray stone, tile 12 uses wood paneling, and tile 5 uses the
fixed prison cell design. Other wall types currently use blue stone. The vector
art is implemented for this demo using the original game's appearance as a
reference. Light positions are maintained separately in `lights.h`.
