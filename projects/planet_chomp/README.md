# Planet Chomp (3DO) - prototype

A 3DO version of [geekychris/planet-chomp](https://github.com/geekychris/planet-chomp),
a Pac-Man homage on a tiny planet, originally a Unity (C#) game. Not a port of the
code, which needs Unity: the maze, movement and camera are rewritten in C89 with
fixed-point maths, and the 3D is drawn by the 3DO cel engine.

| File | |
|---|---|
| `src/maze.c` | the cube-sphere maze (from `SphereMaze.cs`): 6 x 9 x 9 cells on an integer lattice, recursive-backtracker carving, braiding, extra loops; wall boxes |
| `src/render.c` | camera (from `PlanetCamera.cs`), horizon culling, projection (reciprocal table, no divides), bucket depth sort, planet silhouette fan |
| `src/cels.c` | flat-shaded quads as cels (one texel mapped onto four corners) |
| `src/main_3do.c` | the chomper on rails with screen-relative steering, camera follow, HUD |

Pad: D-pad steers (a turn waits for the next junction), L/R spin the view,
C toggles the whole-planet view, X quits.

Status: prototype - maze, camera, walls, crumbs and steering. About 17 fps
drawing (around 110 walls and 380 cels a frame), game logic at 50 steps/s.
Still to do: the four spooks and their AI, keys and the frightened mode,
lives, levels, title and game-over screens, sounds, the radar, sprites for
the chomper and spooks, posts at wall joints.
