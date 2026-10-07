# 3DO programming notes

Things found while building this environment and the demo, verified in the
emulator. Add to this as you learn more.

## Debug output

- The kernel writes debug output a byte at a time to the MADAM ID register,
  which is what the harness captures. Boot messages from the kernel and
  folios show up there too, which is handy for checking that the disc booted.
- **`kprintf()` only formats the first three varargs correctly.** It's
  declared `__swi(0x1000e)`, so arguments 4 and up go on the user stack, and the
  kernel doesn't read them from there. Example:
  `kprintf("%d %d %d %d\n",1,2,3,4)` prints garbage for the 4th. Use
  `tdo_log()` (sdk/include/tdo.h), which formats with `vsprintf` and then
  calls `kprintf("%s", buf)`.
- A program that returns from `main()` gets relaunched by the OS. On-target
  tests should print `TDO:DONE` once; the runner only judges the first run.

## Cels (MADAM)

- `CreateCel(w, h, 16, CREATECEL_UNCODED, buf)` gives an opaque 1:1 cel at
  0,0 with absolute pointers (`CCB_NPABS|SPABS|PPABS`) and `CCB_LAST` set.
  Clear `CCB_LAST` on every cel except the end of the list, or the cel engine
  stops early.
- Fixed point: `ccb_XPos/YPos` 16.16; `ccb_HDX/HDY` **12.20** (so `<< 20` for
  1.0); `ccb_VDX/VDY` 16.16. Rotation+zoom: HDX=cos·z, HDY=sin·z,
  VDX=-sin·z, VDY=cos·z (see `place_rotated` in projects/demo/src/main.c).
- Uncoded 16bpp pixels are `0RRRRRGGGGGBBBBB`; the value 0 is transparent
  unless `CCB_BGND` is set. Keep "black" as 0x0001 if it must be opaque.
- Several cels can share one pixel buffer, and you can swap `ccb_SourcePtr`
  every frame. That makes colour cycling and font glyph changes free.
- PIXC (per half-word): `1S_PDC | MS_CCB | MF_k | SF_8 | 2S_0 | 2D_1`
  outputs `pixel * k/8`, which is good for per-cel brightness (demo stars).
  `0x1F00` is plain opaque. `2S_CFBD | 2D_1` adds the framebuffer
  (additive), but where texels of a magnified cel overlap at their edges the
  seams get added twice and show up as a bright grid.
- `DrawCels(bitmapItem, firstCCB)` once per frame with one linked list is the
  cheap path. The demo draws ~110 cels per frame at 60 fps.

## Performance (Opera timing, ARM60 @ 12.5 MHz)

- A 64×48 per-pixel software plasma (4 table lookups and a palette lookup
  per pixel) plus ~110 cels fits in a frame. At 80×60 it drops to 30 fps.
- Measure, don't guess: print a status line every N frames and count
  emulator frames between two of them (projects/demo/test.py does this).

## Display / input

- `CreateBasicDisplay(&sc, DI_TYPE_DEFAULT, 2)` gives two 320×240 screens.
  Pattern: draw into `sc_BitmapItems[cur]`, then `DisplayScreen(sc_Screens[cur],0)`,
  flip `cur`, then `WaitVBL(GetVBLIOReq(), 1)`.
- `DisableHAVG/DisableVAVG` on each screen gives crisp pixels.
- `DoControlPad(1, &buttons, continuousMask)` returns edge-triggered
  presses, except for the bits in `continuousMask`, which report while held.
- A button that's already down on the first `DoControlPad` call never
  registers (no edge). Scripted tests should wait ~30 frames after the
  "ready" log line before pressing.
- Emulator mapping: 3DO A/B/C = libretro Y/B/A, P = Start, X = Select.

## Toolchain

- Norcroft ARM C 4.91 is C89 with a few extensions (`__swi`, `__value_in_regs`).
  Lots of "padding inserted in struct" warnings from SDK headers are
  harmless.
- `sizeof(int)==4`, big-endian, struct alignment is 4 (a `u8,u32,u16` struct
  is 12 bytes). Signed `/` truncates toward zero, and `>>` on signed values is
  arithmetic. `projects/unittest` asserts all of these.
- The libs come from Portfolio 2.5 (OS v24). The default BIOS `panafz10.bin`
  runs only signed discs, and `3dt pack` signs them. `panafz10-norsa.bin`
  runs unsigned ones.
