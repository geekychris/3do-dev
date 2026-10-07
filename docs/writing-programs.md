# Writing 3DO programs

A practical tutorial using the code in this repo. It assumes C, not 3DO,
knowledge.

## The project

```sh
./3do new mygame
```

creates:

```
projects/mygame/
  Makefile         NAME := mygame + include ../../sdk/project.mk
  src/main.c       your program (all src/*.c, *.cpp, *.s are compiled)
  test.py          headless smoke test (./3do test mygame)
```

Optional extras the build picks up automatically:

| Path | Becomes |
|---|---|
| `takeme/...` | files on the disc root (load them with the filesystem folio) |
| `assets/foo.png` | `cels/foo.cel` on the disc (16bpp uncoded cel via `3it`) |
| `banner.png` | the boot splash (`BannerScreen`), 320×240 |

Makefile knobs: `STACKSIZE := 16384`, `EXTRA_CFLAGS`, `EXTRA_LIBS`, and
`DEBUG=1` on the command line for `-O0`.

## The language: C89 for Norcroft

- Declare variables at the **top of a block**. Use `/* */` comments.
- `int`, `long` and pointers are 32 bits; memory is **big-endian**; structs
  are 4-byte aligned.
- **No FPU.** `float` works through slow software emulation. Use 16.16 fixed
  point (`frac16`) and the math folio: `MulSF16`, `DivSF16`, `SinF16`,
  `CosF16`, `SqrtF16` (call `OpenMathFolio()` first). Angles are **256 units
  per circle**.
- Types: `s32 u32 s16 u16 u8 Err Item` from `types.h`.

`projects/unittest` verifies these assumptions on the emulated hardware.

## The main loop

The template (`sdk/template/src/main.c`) shows the standard shape:

```c
if(OpenGraphicsFolio() < 0 || InitControlPad(1) < 0 ||
   CreateBasicDisplay(&sc, DI_TYPE_DEFAULT, 2) < 0)   /* 2 screens = double buffer */
  return 1;
vbl = GetVBLIOReq();

for(;;)
  {
    DoControlPad(1, &buttons, ControlUp | ControlDown | ControlLeft | ControlRight);
    if(buttons & ControlX) break;

    /* draw into sc.sc_BitmapItems[screen] ... */

    DisplayScreen(sc.sc_Screens[screen], 0);   /* show what we drew */
    screen = !screen;                           /* draw into the other one next */
    WaitVBL(vbl, 1);                            /* 60 Hz */
  }
```

`DoControlPad` reports **new presses** (edge-triggered), except for bits
you pass in its third argument, which report while held (the D-pad above).
Buttons: `ControlA/B/C`, `ControlStart` (P), `ControlX`, `ControlLeftShift/RightShift`,
`ControlUp/Down/Left/Right`.

## Drawing with cels

`FillRect` and `DrawText8` (used by the template) are fine for simple
things. Real 3DO graphics are **cels**: rectangles of pixels that the MADAM
cel engine draws with any position, scale, rotation, skew and blend mode,
essentially for free. The demo (`projects/demo/src/main.c`) draws about 110
cels per frame at 60 fps.

```c
CCB *c = CreateCel(32, 32, 16, CREATECEL_UNCODED, NULL);   /* 16bpp, own pixel buffer */
u16 *pixels = (u16 *)c->ccb_SourcePtr;                     /* 0RRRRRGGGGGBBBBB, 0 = transparent */

c->ccb_XPos = Convert32_F16(100);   /* position: 16.16 */
c->ccb_YPos = Convert32_F16(50);
c->ccb_HDX  = 2 << 20;              /* horizontal step per source pixel: 12.20 (2x wide) */
c->ccb_VDY  = 2 << 16;              /* vertical step per source row: 16.16 (2x tall) */

DrawCels(sc.sc_BitmapItems[screen], c);
```

- **Rotation/zoom:** set the 2×2 matrix `HDX/HDY/VDX/VDY` from cos/sin × zoom
  (`place_rotated` in the demo).
- **Lists:** link cels through `ccb_NextPtr` and draw them all with one
  `DrawCels`. Clear `CCB_LAST` on every cel except the final one.
- **Sharing pixels:** many cels can point at one buffer, and swapping
  `ccb_SourcePtr` each frame is free. The demo's font and colour cycling work
  this way.
- **Blending:** `ccb_PIXC`. `0x1F001F00` is opaque. `(PPMPC_MF_k | PPMPC_SF_8 ...)`
  scales brightness by k/8, and `PPMPC_2S_CFBD` adds the framebuffer
  (additive/translucent). See `docs/3do-notes.md` for details and pitfalls.
- **Images:** drop PNGs in `assets/` and load them at run time with
  `LoadCel("cels/foo.cel", MEMTYPE_CEL)`.

Software rendering is slow on a 12.5 MHz CPU: the demo can afford a 64×48
software plasma (stretched by a cel) at 60 fps, but 80×60 drops it to 30.
**Measure** (see below) rather than guess.

## Logging

```c
#include "tdo.h"
tdo_log("mygame: state=%d x=%d y=%d lives=%d\n", state, x, y, lives);
```

Output appears in the window's terminal, `./3do log`, `./3do ctl log` and
Claude's `emu_log`. Prefer `tdo_log` to `kprintf`, which garbles arguments
after the third. Log meaningful state changes with a prefix: tests and
agents key off these lines.

## Testing

**On the console** (fast, exercises the real compiler and OS):

```c
#include "tdo.h"
tdo_check_eq("score/after-hit", score, 100);
tdo_check("list/not-empty", head != NULL);
tdo_test_done();                     /* prints TDO:DONE pass=.. fail=.. */
```

`./3do test <project>` boots it headless and exits non-zero on failure (see
`projects/unittest`).

**From the host** (`projects/<p>/test.py`), driving the program like a
player:

```python
with Emulator() as emu:
    emu.load(ISO)
    assert emu.run_until_log("mygame: ready", 1500)
    emu.step(30)                      # let the main loop start before pressing
    emu.press("A")
    assert emu.run_until_log("mygame: jump", 60)
    emu.screenshot("build/after-jump.png")
```

Measuring frame rate: print a status line every N frames and count emulator
frames between two of them (`projects/demo/test.py` does exactly this).

## Debugging

```sh
./3do run mygame                     # in one terminal
./3do gdb mygame --attach            # in another
(gdb) break main.c:120
(gdb) continue
(gdb) bt
(gdb) p player
(gdb) watch score
(gdb) monitor screenshot path=build/at-bp.png
```

Without gdb: `./3do ctl break addr=update_player`, `./3do ctl regs`,
`./3do ctl disasm`, `./3do ctl watch addr=score`, `./3do ctl read_u32 addr=score`.
Use `./3do ctl ps` to see your task's state (waiting on VBL = healthy idle).

## Where to learn more

- `third_party/3do-devkit/docs/3dosdk/`: the original SDK documentation (HTML).
  Grep it for any function name.
- `third_party/3do-devkit/examples/`: original and reworked SDK examples.
- `projects/demo`: cels, fixed point, input, logging and a host-side test in one file.
- [3do-notes.md](3do-notes.md): verified facts and gotchas.
- https://3dodev.com: community wiki, docs and tools.
