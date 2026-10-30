# Emberkeep (3DO)

A 3DO port of Emberkeep, the top-down lantern-in-the-dark cavern explorer from
[geekychris/amiga_games](https://github.com/geekychris/amiga_games)
(`emberkeep/`, an AGA game in C++). Collect the runes to open the exit portal
and keep your lantern burning with oil flasks: it only lights a small square
around you, and the light shrinks as the oil runs out. Four chambers.

![title](../../docs/images/games/emberkeep-title.png)
![playing](../../docs/images/games/emberkeep-play.png)

## Playing

| Pad | |
|---|---|
| D-pad | walk |
| A or P | start; back to the title from the end screen |
| C (title) | a recorded run through chamber 1 that hands over to you at chamber 2 |
| X | back to the arcade menu |

Silent, as on the Amiga (its sound functions are stubs).

## How it was ported

The game is C++ on the Amiga, but simple C++ (one class with methods, references,
`bool`), so it was converted to C89 for Norcroft rather than built with armcpp:

| File | |
|---|---|
| `src/game.c`, `src/game.h` | `game.cpp`: the `EmberGame` methods as `game_*` functions over the same `GameState`; references became pointers, `bool` became `int`. The rules are unchanged |
| `src/levels.c`, `src/levels.h` | `levels.cpp` (one fix, below), structs as typedefs |
| `src/tilesheet_data.c` | the baked 20x8 sheet of 16x16 tiles, unchanged |
| `src/render.c` | `render.cpp` on the sdk/amiga layer. The tiles are copied straight into the layer's 8-bit pen buffer instead of one `RectFill` per run of same-coloured pixels, and since that buffer persists between frames, cells outside the lantern light are only filled black once when they go dark; the cells the light touches are redrawn every frame, with the sprites and the lantern mask clipped to them (the same picture as the Amiga's draw-everything-then-mask) |
| `src/main_3do.c` | `main.cpp`: the pad instead of IDCMP raw keys, `gfx_swap()` instead of `ChangeScreenBuffer` + `WaitTOF`, `gfx_steps()` so the logic keeps its 50 ticks per second |
| `src/demo.h` | the recorded run (port addition): per-tick input, so it plays the same at any frame rate |

### Fixes to the original

- **Chamber 1 could not be completed:** the rune at column 6, row 6 sits in a
  walled box with no way in. Two tiles of the box's top wall are opened
  (`FIX_SEALED_RUNE` in `levels.c`; 0 gives the original layout). Found by a breadth-first search over the game's tick
  function compiled for the host, which also produced `demo.h`.
- The title and end-screen captions (pen 0 on a coloured bar) are black boxes
  on the Amiga, whose default draw mode is JAM2; the port draws text in JAM1.

## Testing

`./3do test emberkeep`: boots, starts the recorded run from the title (C),
checks that it clears chamber 1 and chamber 2 starts, walks around, checks the
speed (50 fps, 50 logic steps/s) and quits with X. The game logs a
`EMBERKEEP state=...` line on every change of mode, level, lives or runes,
`demo: start` / `demo: done` and a `status:` line every 250 frames.
