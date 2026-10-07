# Amiga → 3DO compatibility layer

Lets Amiga games written against graphics.library (SetAPen, RectFill,
Move/Draw, Text, WritePixel, Area*, ...) run on the 3DO with their game and
drawing code unchanged. Used for the ports of
[geekychris/amiga_games](https://github.com/geekychris/amiga_games) (and the
Atari ports in hatari_augmented), all playable from the **arcade** disc
(`./3do run arcade`).

## How it works

- **Graphics:** the game draws into an offscreen 320×256 buffer, like an Amiga
  low-res screen. `gfx_swap()` shows it as one hardware cel scaled to the 3DO's
  320×240. In the default `GFX_PAL32` mode a pixel is a pen number (0–31)
  and the cel's PLUT is the palette, so palette writes recolour the whole
  screen instantly, as on the Amiga. `GFX_RGB16` (256-colour AGA games)
  stores 15-bit colour instead.
- **Timing:** `gfx_swap()` paces to **50 fps** (PAL Amiga speed) on the 60 Hz
  3DO against an absolute VBL count, so a slow frame doesn't cost a whole
  extra VBL. Every 250 frames it logs `AMIGA3DO: frame=N`, which tests use
  to measure speed.
- **Input:** `pad_held(port)` / `pad_pressed(port)` return `PAD_*` bits
  (UP DOWN LEFT RIGHT A B C P X L R). **X quits to the menu**
  (`amiga_quit_requested()`).
- **Files:** `Open/Read/Seek/Close` read from the disc. `"PROGDIR:x"` or `"x"`
  maps to `$boot/<progdir>/x`, set with `amiga_set_progdir("<game>")`. Writes
  (`MODE_NEWFILE`) go to NVRAM (`/NVRAM/<name>`, 32 KB total, so keep them
  tiny). `amiga_load_file()` loads a whole file.
- **Sound:** a Paula emulation (`custom.aud[n]`, `paula_dmacon()`) mixed in
  software at 22 kHz and streamed to the 3DO audio folio, plus a C ProTracker
  player with the ptplayer API (`mt_install_cia`, `mt_init`, `mt_playfx`,
  `mt_soundfx`, `mt_end`, ...). Music ticks run at 50 Hz of *audio* time, so
  tempo holds even if a game draws slower. Load MODs with
  `amiga_load_file("PROGDIR:x.mod", &len)`. Code that writes Paula registers
  itself works too: start channels with `paula_dmacon(DMAF_SETCLR|DMAF_AUDn)`
  (plain `custom.dmacon =` writes are applied once per frame), and use
  `paula_init(tick, 50)` for a player tick.
- **Debug bridge:** `ab_init`, `AB_I/W/E(fmt, ...)` (real functions here, since
  Norcroft has no variadic macros) print to the 3DO debug console with the name
  given to `ab_init` as prefix. `ab_register_var/hook` are no-ops.

## Porting a game

```
projects/<game>/
  Makefile        NAME := <game>  +  include ../../sdk/amiga/amiga.mk
  arcade.txt      title=, origin=AMIGA|ATARI ST|..., order=NN, desc=...  (menu entry)
  src/            game.c, draw.c, ... (Amiga originals) + main_3do.c
  takeme/<game>/  data files (MODs, samples) -> $boot/<game>/ on both discs
  test.py         headless test using tdo.porttest
```

1. Copy the game's portable sources (`game.c`, `draw.c`, headers, ...) to
   `src/`. Don't copy `main.c` (keep it as `main.c.amiga` if you want it for
   reference; anything not ending in `.c` isn't compiled), `input.c`, or asm files.
2. Write `src/main_3do.c` (see `projects/rock_blaster/src/main_3do.c`):
   - `ab_init("NAME")`, `amiga_set_progdir("<game>")` if it loads files.
   - `gfx_init(palette, ncolors)` (or `gfx_init_mode(GFX_RGB16, 256, ...)`).
   - The original main loop: read input, update, draw into `gfx_back()`,
     `gfx_swap()`. Leave the loop when `amiga_quit_requested()`.
   - Implement the game's input hooks (`input_read()` etc.) from `pad_held()` /
     `pad_pressed()`. Map controls sensibly: D-pad moves, A primary (fire),
     B secondary (jump/thrust), C third, P start/pause, X quit, L/R extras.
     Keep "press fire/start to begin" working with A *and* P.
   - Copy the palette and any procedural sound/music setup from the original
     `main.c`.
   - Log state changes with `AB_I("state=PLAYING level=%d score=%d", ...)`;
     tests and agents rely on these lines.
   - On exit: free what you allocated and call `gfx_exit()`, then return from
     `main`. The arcade menu runs again after the game exits, so leaks add up.
   *Alternative for games whose `main.c` holds the game logic* (Bullion
   Dash, Uranus Lander, Jump Quest): keep `main.c` and put its AmigaOS-only
   parts (OpenLibrary, OpenScreen, IDCMP window, Ctrl-C, `WaitTOF()` before the
   swap - `gfx_swap()` already paces) under `#ifndef AMIGA3DO`, then provide
   `gfx_3do.c` / `input_3do.c` for the game's own gfx/input API.
   `python3 sdk/amiga/tools/amigamain.py src/main.c <game>` does the usual
   main.c conversions (display functions, libraries, IDCMP loop, Ctrl-C,
   back buffer, WaitTOF, DH2: paths) and lists what it changed. If the game's
   functions are called `gfx_init`/`gfx_swap` like the layer's, rename them
   with `#define gfx_init xx_gfx_init` in the game header (and call the layer
   from a file that doesn't include that header).
3. Fix what Norcroft (C89) rejects: **declarations after statements**,
   `for (int i ...)`, `inline`, compound literals `(T){...}`, variadic macros,
   and `long long` arithmetic (it compiles but the runtime helpers are missing:
   rewrite with 32-bit maths). `//` comments are fine. Plain `char` is
   unsigned, so use `BYTE`/`signed char` for signed samples.
   `python3 sdk/amiga/tools/c89fix.py src/*.c` hoists late declarations and
   `for (int ...)` automatically and lists what it can't move (run it
   *before* adding `#ifdef AMIGA3DO` blocks: it doesn't follow the
   preprocessor and may hoist into an `#else` branch); compound
   literals and other cases still need hand edits. The 3DO headers define
   `Item`; a game type with that name needs `#define Item xx_Item`.
4. Height: screens are 256 lines tall and are scaled to 240 by default. Use
   `gfx_set_view(GFX_VIEW_CROP, y0)` to show 240 lines 1:1 instead, if the game
   leaves the top/bottom 16 lines empty.
5. Update on-screen instructions that name keyboard keys to the 3DO pad.
6. `./3do build <game>`, `./3do run <game>`, then write `test.py` (copy
   `projects/rock_blaster/test.py`) and make `./3do test <game>` pass.
   `./3do profile <game> --press P@400` shows where time goes if it's slow.

7. Audio: code that writes `custom.aud[n]` keeps working; replace writes to
   `custom.dmacon` / `DMACON` with `paula_dmacon(...)` and point raw register
   macros (`0xDFF0A0`...) at `&custom.aud[n]`. Data files go in
   `takeme/<game>/` and load via `PROGDIR:` after `amiga_set_progdir()`.
   Don't ship third-party music (covers of commercial songs): load it if
   present and run with sound effects only otherwise.

## Performance notes (ARM60 @ 12.5 MHz)

The ARM60 has no cache and no divide or halfword instructions, so code that
the Amiga's blitter made cheap (hundreds of small RectFills, per-pixel
WritePixel loops, redrawing static tiles every frame) is the usual problem.
In order of preference:

- **Measure**: `./3do profile <game> --press A@450`.
- **Layer helpers**: `gfx_sprite_make`/`gfx_sprite_draw` (pre-rendered
  run-length sprites/glyphs instead of per-pixel drawing), `gfx_fill_columns`
  (height-map scenery as row spans), `gfx_blit8`.
- **Cache what doesn't change**: if a background depends only on a few
  inputs (tile ids, scroll column), draw it once per change and `memcpy` it
  afterwards (see bullion_dash/render.c, jump_quest/level.c,
  pea_shooter_blast/draw.c). Verify the cache by rendering both ways in a
  debug build and comparing every pixel.
- **Remove divisions** from per-column/per-pixel loops (step indices, tables).
- **`gfx_steps()`** when it still can't draw at 50 fps: run the game logic as
  many 50 Hz steps as are due, draw once, and test with
  `check_game_speed()` - the game keeps its real speed at a lower frame rate.
- The C library `memmove` is not safe for overlapping copies to a higher
  address; copy through a temporary buffer.


- A full-screen `SetRast`/`RectFill` clear costs about a quarter of a 50 fps
  frame. Don't clear more than once per frame.
- `memset` (libc) is fast (STM). Hand-written C byte loops are not.
- There's no hardware divide: avoid divisions in inner loops (use shifts or
  reciprocal tables).
- `Text` and `Draw` have fast paths for unclipped, non-COMPLEMENT drawing.
- Measure with `./3do profile`, which samples at random points inside frames.

## Not supported

Copper lists, hardware sprites, blitter registers, direct bitplane access
(`rp->BitMap->Planes[]`), and Intuition windows, gadgets and menus. Replace
those parts in `main_3do.c` or a game-local file.
