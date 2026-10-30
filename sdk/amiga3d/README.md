# amiga3d on the 3DO

The 3DO platform layer for the `amiga3d` engine from
[amiga_mcp](https://github.com/geekychris/amiga_mcp) (`examples/amiga3d`),
on top of the [`sdk/amiga`](../amiga/README.md) compatibility layer. Twelve
games on that engine build from their Amiga sources for the 3DO here:

| Game | fps (emulator) | Tests on the 3DO | Host cross-check |
|---|---|---|---|
| [Block Breaker 3D](../../projects/block_breaker_3d/README.md) | 14-15 (title 9) | 42 | long autopilot game's hash (`vectors.h`) |
| [Marble Tilt](../../projects/marble_tilt/README.md) | 10-11 | 27 | long autopilot game's hash |
| [Harbour Crane](../../projects/harbour_crane/README.md) | 11-13 | 51 | long autopilot game's hash |
| [Tank Duel](../../projects/tank_duel/README.md) | 13 | 33 | long autopilot game's hash |
| [Orbit Docking](../../projects/orbit_docking/README.md) | 12-13 | 33 | long autopilot game's hash |
| [Stack Tower](../../projects/stack_tower/README.md) | 5-6 | 45 | the browser's four games (`vectors.h`) |
| [Ridge Runner](../../projects/ridge_runner/README.md) | 7-9 | 35 | long autopilot race's hash |
| [Drone Courier](../../projects/drone_courier/README.md) | 16-17 | 30 | long autopilot day's hash |
| [Canyon Rail](../../projects/canyon_rail/README.md) | 15 | 30 | long autopilot ride's hash |
| [Lander 3D](../../projects/lander_3d/README.md) | 4-5 | 49 | `trace_main.c`'s flight, line by line |
| [Boing Pinball](../../projects/boing_pinball/README.md) | 25 (full) | 81 | the browser's vectors |
| [ScAIry Fun](../../projects/scairy_fun/README.md) | 4-5 | 90 | `trace_main.c`'s route, line by line |

The games run at 25 frames a second at most (as on the Amiga) and step
their simulations by the clock (`plat_ticks`), so a game that draws fewer
frames still plays at the right speed, just less smoothly. All of them are
on the arcade disc (`./3do build arcade`).

## A game's project

```
projects/<game>/
  Makefile            NAME := <game>  +  include ../../sdk/amiga3d/a3d.mk
  src/*.c, *.h        the game's Amiga sources (core, shell, rendering)
  src/tests/*.c, *.h  its test cases (run_tests) and tests/vectors.h
  src/pad_3do.c       the pad mapping (a3_pad_keys) and a3_game_status()
  takeme/<game>/      music / voice files, loaded through PROGDIR:
  test.py             sdk/amiga3d/a3dtest.py: the 3DO selftest + the game
  arcade.txt          the arcade menu entry
```

`tools/import_game.sh <game>` copies a game from `../amiga_mcp/examples`
(or `$AMIGA_MCP/examples`) and runs `tools/c89init.py` over it. Then write
`src/pad_3do.c`, fix what Norcroft rejects (below), and add the game to
`tools/padtext.py`, which gives each on-screen key name (the help pages'
controls, "PRESS FIRE", "HELP: NEXT") a 3DO line under `#ifdef AMIGA3DO`
that names the pad's buttons.

`make A3D_TEST=1 BUILD=build/selftest` builds a second disc whose main runs
the game's `run_tests()` and reports each check as `TDO:PASS`/`TDO:FAIL`;
`A3Test.selftest()` in `a3dtest.py` builds and runs it. `A3Test.host_trace()`
builds a trace program natively and compares its output with `TRACE` lines
the test disc printed (Lander 3D, ScAIry Fun).

## The pad

`a3d_3do.h`: the D-pad is the joystick (`game_joy_bits`); each
`a3_pad_keys` entry turns a pad button into joystick bits and/or one of the
game's Amiga raw keys (`game_key_bit`). X always quits to the menu. The
convention used: A = fire / start, P = start (title) and pause, B = the
autopilot, C = the help pages, L = back to the title (Esc), with exceptions
where a game needs more buttons (see each README). Presses are read once a
frame, so at 5 fps a tap shorter than a fifth of a second can be missed.

`a3_game_status()` writes the game's state as `key=value` pairs; `main_3do.c`
logs it as `A3D: st ...` once a second when it changes, for the tests.
`A3D: fps=... fields: update= draw= wait= swap=; a frame: polys= cels=`
every 5 s says where the time goes.

## How the 3DO side works

| File | |
|---|---|
| `src/main_3do.c` | the main (as `main_68k.c` / `main_os4.c`): the frame, pacing to 25 fps against the field counter, the pad, the music (the layer's ptplayer, effects never on the music's two channels), `plat_sky`, the test main (`A3D_TEST`) |
| `src/a3d_draw.c` | the engine's drawing with a 3DO backend: 8-bit pens in the layer's frame, the 3D draw list through the cel engine, 16-bit overlays |
| `src/a3d_math.c`, `src/a3d_fix.c`, `src/a3d_sound.c` | the engine's, with the 64-bit arithmetic replaced |
| `src/a3d_i64.c`, `include/a3d_i64.h` | 64-bit arithmetic in 32-bit pieces |
| `src/a3d_libc.c` | `atol` (the 3DO's libc lacks it) |
| `include/stdint.h` | the C99 types for Norcroft (no `int64_t`, on purpose) |
| `include/bridge_client.h` | the bridge API: logging to the debug console, the rest no-ops, the test harness to the log |

**The frame.** The layer's 320 x 256 frame of 32 pens, shown as one cel
(256 lines on 240: in each 16-line band a repeated line is left out). The
2D calls write pens into it directly.

**The 3D.** `a3_flush` doesn't scan-convert the draw list: each polygon
becomes cels of one texel (its pen's colour, 16-bit) mapped onto
quadrilaterals - a fan of quads, the last maybe a triangle - in the list's
far-to-near order, and the cel engine fills them (as void_trader does its
triangles). The cels go over the frame (`gfx_set_overlay`, a small addition
to `sdk/amiga`). Whatever the game draws after a flush (the HUD) must stay
on top of them, so the canvas then switches to a 16-bit overlay (0 is
transparent), cleared each frame in the 16-line bands that were drawn into;
two overlays allow two flushes with 2D between them (flushes with nothing in
between share one), and after that the software rasteriser draws into the
last overlay. `a3_cels_begin/poly/end` does the same for a game with its own
sorted polygon list (ScAIry Fun). This took Block Breaker 3D from 4.7 to
14.7 fps.

**Drawing in software** (2D polygons, the tall picture, a flush into a
picture): the spans are kept in 32-bit arrays (the ARM60 has no halfword
loads or stores), the rows are clipped once rather than tested each step,
each edge's slope comes from a reciprocal table instead of a divide, and
long spans are filled with STM (`amiga_fill64`).

**Divides.** The ARM60 has no divide instruction; projections and clipping
use a reciprocal table (`q * ceil(2^31 / z) >> 31`: at most one more than
`q / z`, the same for a vertex every time, so shared edges still meet). This
is for drawing only; the simulations keep exact arithmetic. (The emulator
charges up to 16 cycles a multiply, by the multiplier's bits, so this helps
less than an instruction-count profile suggests.)

**`plat_sky`** (the copper's gradient): a 4-pixel-wide 16-bit cel of the
gradient's rows, stretched across the screen under the frame, and the sky
pen made transparent in the frame's palette (`gfx_set_transparent_pen`).
The overlays draw the sky pen in the gradient's colour for its row.

**Pictures.** The 3DO side keeps the classic Amiga's 32-colour look:
`g_image_load` returns NULL (the OS4-only full-colour pictures are not
shipped), `g_pic_*` is a pen buffer copied with `memcpy`.

## 64-bit arithmetic

Norcroft ARM C 4.91 has no `long long` that links, and the ARM60 has no long
multiply. The games' simulations use `FMUL` / `FDIV` (`((int64_t)a * b) >> 16`,
`((int64_t)a << 16) / b`) everywhere, and their trace hashes must match the
host's bit for bit. `a3d_i64.h` gives the same results in 32-bit pieces:

- `a3_mul_sh16` (FMUL): `a*bh + ah*bl + ((al*bl) >> 16)` with 16-bit halves
  (the multiples of 2^16 shift out exactly; only the low 32 bits are kept),
  and a plain `(a * b) >> 16` when both fit in 16 bits.
- `a3_div_sh16` (FDIV): `(|a| / |b|) << 16` plus the remainder's 16 bits (a
  divide when `|b| <= 65536`, else 16 steps of shift-and-subtract), signed
  as C truncates.
- `a3_mul_hi`, `a3_umul_hi`, `a3_muldiv`, `a3_mul_shr`, `a3_dot3_nonneg`
  and an `a3_i64` type with add, compare, shift and divide, for the rest.

They were checked against the host's 64-bit arithmetic on 20 million random
operands (all equal), and every game's trace check passes on the 3DO.

## Norcroft C89, what the games needed

- Run-time aggregate initialisers (`const int32_t v[] = { g->a, ... }` in
  every state hash): `tools/c89init.py` rewrites them as assignments.
- Declarations after statements: by hand (a few).
- `static inline` in headers: `-Dinline=__inline` (a3d.mk).
- Names the 3DO's headers take: `s16` (types.h) and `Item` - `#define s16 xx_s16`.
- `int64_t` / `uint64_t`: through `a3d_i64.h` under `#ifdef AMIGA3DO`.
- `sscanf` is missing (one bridge hook left out); `atol` is in `a3d_libc.c`.
- Plain `char` is unsigned: the engine's samples are `int8_t` (signed char).

`sdk/amiga/tools/c89fix.py` (hoisting declarations) is best not run over
files that already compile: it rewrote working code and once put an
initialiser above its declaration.

## Not done

- The OS4 builds' full-colour pictures (title paintings, sky panoramas) are
  not used: the 3DO shows the classic Amiga's 32-colour look.
- The slow games (Stack Tower, Lander 3D, ScAIry Fun at 4-6 fps) spend most
  of the frame building, projecting and clipping 200-400 polygons on a
  12.5 MHz CPU; making them smooth would take game-side changes (fewer,
  larger polygons; cached projections).
