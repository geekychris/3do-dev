# Planet Chomp (3DO)

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

Pad: D-pad steers (relative to the screen; a turn waits for the next
junction), L/R spin the view, C whole-planet view, P pause, A starts,
X quits to the menu. Left alone on the title, it plays a demo.

Status: complete game - title screen and attract mode (an autopilot plays
after 15 s, ported from the Unity bot), the four spooks with their arcade
personalities (Ghost.cs), keys and the frightened mode, lives, levels with a
new maze each, high score in NVRAM, synthesised sounds on the DSP, the
radar. About 14 fps drawing with ~120 walls and ~400 cels a frame; game
logic at 50 steps/s.

| More files | |
|---|---|
| `src/game.c` | rules (GameDirector.cs), spook AI (Ghost.cs), steering (Chomper.cs), rail movement (Mover.cs), autopilot |
| `src/sprites.c` | chomper, spook and key textures, drawn at startup |
| `src/sfx.c` | the sounds of Sfx.cs, synthesised at startup, played on Paula |
