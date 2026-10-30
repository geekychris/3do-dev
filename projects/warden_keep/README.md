# Warden Keep (3DO)

A 3DO port of Warden Keep, the isometric room adventure from
[geekychris/amiga_games](https://github.com/geekychris/amiga_games)
(`warden_keep/`, C++): one Filmation-style room of stone blocks, barrels you
can push and three jewels to collect.

![title](../../docs/images/games/warden_keep-title.png)
![playing](../../docs/images/games/warden_keep-play.png)

## Playing

| Pad | |
|---|---|
| D-pad | step one tile; hold to repeat |
| A or P | start; back to the title from the end screen |
| X | back to the arcade menu |

No sound, as on the Amiga.

## How it was ported

The game is C++ on the Amiga, but simple C++ (one class with methods,
references, `bool`), so it was converted to C89 for Norcroft rather than built
with armcpp:

| File | |
|---|---|
| `src/game.c`, `src/game.h` | `game.cpp`: the `WardenGame` methods as `game_*` functions over the same `GameState`; the rules are unchanged |
| `src/render.c` | `render.cpp`'s drawing code (RectFill scanlines and Draw lines for the iso cubes and floor diamonds) on the sdk/amiga layer, converted to C. The game is turn-based, so the room is only redrawn when it changed: the layer's pen buffer keeps it between frames |
| `src/main_3do.c` | `main.cpp`: the pad instead of IDCMP raw keys, `gfx_swap()` instead of `ChangeScreenBuffer` + `WaitTOF`; the auto-repeat (first repeat after 12 frames, then every 6) is the original's |

### Fix to the original

The title (`WARDEN  KEEP`) and end-screen captions are drawn in pen 0 over a
coloured bar, which on the Amiga (default draw mode JAM2, background pen 0)
makes them black boxes; the port draws text in JAM1.

## Testing

`./3do test warden_keep`: boots, starts a game, walks over the three jewels,
checks for `state=WIN`, checks the speed (50 fps) and quits with X. The game
logs `WARDEN state=...` on every change of mode or jewel count and
`player wx=.. wy=..` after each step.
