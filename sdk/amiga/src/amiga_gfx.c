/*
 * Amiga graphics.library subset on the 3DO.
 *
 * The game draws into an offscreen buffer (one byte per pixel holding the
 * pen, or 16-bit colour in GFX_RGB16 mode). gfx_swap() hands that buffer to
 * the MADAM cel engine as a single cel: in GFX_PAL32 mode it is an 8-bit
 * *coded* cel whose 32-entry PLUT is the Amiga palette, so the hardware does
 * the colour lookup (and palette writes recolour the screen instantly, as
 * on the Amiga); the cel's vertical step scales 256 lines onto 240.
 */
#include "displayutils.h"
#include "graphics.h"
#include "celutils.h"
#include "debug.h"
#include "mem.h"
#include "stdlib.h"
#include "string.h"

#include "amiga_internal.h"
#include "font8x8_basic.h"

#define W GFX_WIDTH

ULONG g_amiga_frame;

static int    s_mode = GFX_PAL32;
static int    s_h = 256;
static int    s_view = GFX_VIEW_SCALE;
static int    s_view_y0;
static int    s_rate = 50;
static u32    s_base_field;        /* VBL count when pacing (re)started */
static u32    s_paced;             /* frames shown since s_base_field */
static int    s_cur;
static int    s_ready;
static UBYTE *s_pix8;
static UWORD *s_pix16;
static UWORD  s_pal[256];          /* pen -> 0RRRRRGGGGGBBBBB */
static UWORD *s_plut;              /* the cel's hardware PLUT (GFX_PAL32) */
static UBYTE  s_xor_mask = 31;
static CCB   *s_cel;
static ScreenContext s_sc;
static Item   s_vbl = -1;
static struct RastPort s_rp;

/* ------------------------------------------------------------------ colour */

static UWORD
rgb4_to_15(UWORD c)
{
  UWORD r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
  return (UWORD)((((r << 1) | (r >> 3)) << 10) | (((g << 1) | (g >> 3)) << 5) | ((b << 1) | (b >> 3)));
}

static void
set_pen_rgb15(int pen, UWORD c)
{
  if(pen < 0 || pen > 255)
    return;
  s_pal[pen] = c;
  if(s_plut && pen < 32)
    s_plut[pen] = c;
}

void gfx_set_rgb4(int pen, UWORD rgb4)   { set_pen_rgb15(pen, rgb4_to_15(rgb4)); }

void
gfx_set_rgb24(int pen, ULONG rgb)
{
  set_pen_rgb15(pen, (UWORD)((((rgb >> 19) & 31) << 10) | (((rgb >> 11) & 31) << 5) | ((rgb >> 3) & 31)));
}

void
gfx_load_rgb4(const UWORD *rgb4, int n)
{
  int i;
  for(i = 0; i < n; i++)
    gfx_set_rgb4(i, rgb4[i]);
}

void SetRGB4(struct ViewPort *vp, LONG pen, ULONG r, ULONG g, ULONG b)
{
  (void)vp;
  gfx_set_rgb4((int)pen, (UWORD)(((r & 15) << 8) | ((g & 15) << 4) | (b & 15)));
}

void SetRGB32(struct ViewPort *vp, ULONG pen, ULONG r, ULONG g, ULONG b)
{
  (void)vp;
  gfx_set_rgb24((int)pen, ((r >> 24) << 16) | ((g >> 24) << 8) | (b >> 24));
}

void LoadRGB4(struct ViewPort *vp, const UWORD *colors, LONG count)
{
  (void)vp;
  gfx_load_rgb4(colors, (int)count);
}

void LoadRGB32(struct ViewPort *vp, const ULONG *t)
{
  (void)vp;
  while(t && *t)
    {
      ULONG n = t[0] >> 16, first = t[0] & 0xFFFF, i;
      t++;
      for(i = 0; i < n; i++, t += 3)
        SetRGB32(0, first + i, t[0], t[1], t[2]);
    }
}

ULONG GetRGB4(struct ColorMap *cm, LONG pen)
{
  UWORD c = s_pal[pen & 255];
  (void)cm;
  return ((ULONG)((c >> 11) & 15) << 8) | ((ULONG)((c >> 6) & 15) << 4) | ((c >> 1) & 15);
}

/* ------------------------------------------------------------------ setup */

static void
update_view(void)
{
  if(!s_cel)
    return;
  s_cel->ccb_XPos = 0;
  s_cel->ccb_HDX  = 1 << 20;
  s_cel->ccb_HDY  = 0;
  s_cel->ccb_VDX  = 0;
  if(s_view == GFX_VIEW_CROP || s_h <= 240)
    {
      s_cel->ccb_YPos = -((s_view == GFX_VIEW_CROP ? s_view_y0 : 0) << 16);
      s_cel->ccb_VDY  = 1 << 16;
    }
  else
    {
      s_cel->ccb_YPos = 0;
      s_cel->ccb_VDY  = (240 << 16) / s_h;
    }
}

int
gfx_init(const UWORD *pal, int n)
{
  return gfx_init_mode(GFX_PAL32, 256, pal, n);
}

int
gfx_init_mode(int mode, int height, const UWORD *pal, int n)
{
  int i;

  if(s_ready)
    return 1;
  s_mode = mode;
  s_h = (height > 0 && height <= 512) ? height : 256;
  if(OpenGraphicsFolio() < 0)
    return 0;
  if(CreateBasicDisplay(&s_sc, DI_TYPE_DEFAULT, 2) < 0)
    {
      amiga_log("gfx_init: CreateBasicDisplay failed\n");
      return 0;
    }
  for(i = 0; i < 2; i++)
    {
      DisableHAVG(s_sc.sc_Screens[i]);
      DisableVAVG(s_sc.sc_Screens[i]);
    }
  s_vbl = GetVBLIOReq();

  if(mode == GFX_RGB16)
    {
      s_pix16 = (UWORD *)malloc(W * s_h * 2);
      if(!s_pix16)
        return 0;
      memset(s_pix16, 0, W * s_h * 2);
      s_cel = CreateCel(W, s_h, 16, CREATECEL_UNCODED, s_pix16);
      s_xor_mask = 255;
    }
  else
    {
      s_pix8 = (UBYTE *)malloc(W * s_h);
      if(!s_pix8)
        return 0;
      memset(s_pix8, 0, W * s_h);
      s_cel = CreateCel(W, s_h, 8, CREATECEL_CODED, s_pix8);
      s_plut = s_cel ? (UWORD *)s_cel->ccb_PLUTPtr : 0;
      s_xor_mask = 31;
    }
  if(!s_cel)
    {
      amiga_log("gfx_init: CreateCel failed (out of memory?)\n");
      return 0;
    }
  /* draw pen-0 / black pixels instead of treating them as transparent */
  s_cel->ccb_Flags |= CCB_BGND | CCB_LAST;
  update_view();

  for(i = 0; i < 256; i++)
    s_pal[i] = 0;
  if(s_plut)
    for(i = 0; i < 32; i++)
      s_plut[i] = 0;
  if(pal)
    gfx_load_rgb4(pal, n);

  InitRastPort(&s_rp);
  amiga_input_init();
  amiga_audio_init();
  s_cur = 0;
  QueryGraphics(QUERYGRAF_TAG_FIELDCOUNT, &s_base_field);
  s_paced = 0;
  g_amiga_frame = 0;
  s_ready = 1;
  amiga_log("AMIGA3DO: ready %s %dx%d %s\n", g_amiga_name ? g_amiga_name : "game",
            W, s_h, mode == GFX_RGB16 ? "rgb16" : "pal32");
  return 1;
}

void
gfx_exit(void)
{
  if(!s_ready)
    return;
  s_ready = 0;
  amiga_audio_exit();
  amiga_input_exit();
  if(s_cel)
    {
      /* buffer and PLUT: we passed the pixel buffer in, CreateCel owns the PLUT */
      DeleteCel(s_cel);
      s_cel = 0;
    }
  s_plut = 0;
  if(s_pix8)
    free(s_pix8);
  if(s_pix16)
    free(s_pix16);
  s_pix8 = 0;
  s_pix16 = 0;
  if(s_vbl >= 0)
    DeleteItem(s_vbl);
  s_vbl = -1;
  DeleteBasicDisplay(&s_sc);
  amiga_log("AMIGA3DO: exit %s frame=%d\n", g_amiga_name ? g_amiga_name : "game", (int)g_amiga_frame);
}

void gfx_set_view(int mode, int y0) { s_view = mode; s_view_y0 = y0; update_view(); }
void gfx_set_rate(int hz)          { s_rate = (hz == 60) ? 60 : 50; }
ULONG gfx_frame(void)              { return g_amiga_frame; }

static u32   s_step_base;
static ULONG s_steps_done, s_steps_logged;
static int   s_steps_started;

int
gfx_steps(void)
{
  u32 now;
  ULONG due;
  int n;
  QueryGraphics(QUERYGRAF_TAG_FIELDCOUNT, &now);
  if(!s_steps_started)
    {
      s_steps_started = 1;
      s_step_base = now;
      s_steps_done = 0;
    }
  /* logic steps due by now: 50 per second of 60 Hz fields (or 60 at 60 fps) */
  due = (s_rate == 50) ? ((now - s_step_base) * 5) / 6 : (now - s_step_base);
  n = (int)(due - s_steps_done);
  if(n < 1)
    n = 1;
  if(n > 4)
    {
      /* far behind (loading, debugger): don't fast-forward, re-sync */
      n = 4;
      s_step_base = now;
      s_steps_done = 0;
      due = 0;
    }
  else
    s_steps_done += n;
  {
    int i;
    for(i = 0; i < n; i++)
      if((++s_steps_logged % 250) == 0)
        amiga_log("AMIGA3DO: steps=%d\n", (int)s_steps_logged);
  }
  return n;
}
int gfx_height(void)               { return s_h; }
UBYTE *gfx_pixels8(void)           { return s_pix8; }
UWORD *gfx_pixels16(void)          { return s_pix16; }
struct RastPort *gfx_back(void)    { return &s_rp; }

void
amiga_wait_vbls(int n)
{
  if(s_vbl >= 0 && n > 0)
    WaitVBL(s_vbl, n);
}

void
gfx_swap(void)
{
  if(!s_ready)
    return;
  DrawCels(s_sc.sc_BitmapItems[s_cur], s_cel);
  DisplayScreen(s_sc.sc_Screens[s_cur], 0);
  s_cur ^= 1;
  /* Absolute pacing: frame n is due at field base + n*6/5 (50 fps on the
     60 Hz display) or base + n (60 fps). Waiting for "the next VBL" after
     rendering would lose a whole VBL whenever a frame takes more than one. */
  {
    u32 now, due;
    s_paced++;
    due = s_base_field + (s_rate == 50 ? (s_paced * 6) / 5 : s_paced);
    QueryGraphics(QUERYGRAF_TAG_FIELDCOUNT, &now);
    if((s32)(due - now) > 0)
      WaitVBL(s_vbl, due - now);
    else if((s32)(now - due) > 6)
      {
        /* running slow: re-sync instead of rushing to catch up */
        s_base_field = now;
        s_paced = 0;
      }
  }
  g_amiga_frame++;
  /* tests measure speed from the emulator VBLs between these lines
     (50 fps = 300 VBLs per 250 frames) */
  if((g_amiga_frame % 250) == 0)
    amiga_log("AMIGA3DO: frame=%d\n", (int)g_amiga_frame);
  amiga_input_poll();
  amiga_audio_frame();
}

void WaitTOF(void)       { amiga_wait_vbls(1); }
void WaitBlit(void)      { }
void OwnBlitter(void)    { }
void DisownBlitter(void) { }

/* ------------------------------------------------------------------ pixels */

static int
clip_rect(struct RastPort *rp, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  if(*x0 < rp->clip_x0) *x0 = rp->clip_x0;
  if(*y0 < rp->clip_y0) *y0 = rp->clip_y0;
  if(*x1 > rp->clip_x1) *x1 = rp->clip_x1;
  if(*y1 > rp->clip_y1) *y1 = rp->clip_y1;
  return (*x0 <= *x1) && (*y0 <= *y1);
}

/* Byte fill: memset for short runs, the STM routine in fill.s for long ones. */
extern void amiga_fill64(void *dst, ULONG pattern, ULONG blocks);

static void
fill8(UBYTE *p, int pen, long n)
{
  ULONG blocks;
  if(n < 96)
    {
      memset(p, pen, n);
      return;
    }
  while(((ULONG)p & 3) && n)
    {
      *p++ = (UBYTE)pen;
      n--;
    }
  blocks = (ULONG)n >> 6;
  amiga_fill64(p, (ULONG)(pen & 255) * 0x01010101UL, blocks);
  p += blocks << 6;
  n &= 63;
  if(n)
    memset(p, pen, n);
}

/* Horizontal span x0..x1 (inclusive, already clipped) in pen, honouring COMPLEMENT. */
static void
span(struct RastPort *rp, LONG x0, LONG x1, LONG y, int pen)
{
  LONG n = x1 - x0 + 1;
  if(s_pix8)
    {
      UBYTE *p = s_pix8 + y * W + x0;
      if(rp->DrawMode & COMPLEMENT)
        while(n--) *p++ ^= s_xor_mask;
      else if(n < 8)
        while(n--) *p++ = (UBYTE)pen;
      else
        fill8(p, pen, n);       /* long spans: STM fill (fill.s) */
    }
  else if(s_pix16)
    {
      UWORD *p = s_pix16 + y * W + x0;
      if(rp->DrawMode & COMPLEMENT)
        while(n--) { *p ^= 0x7FFF; p++; }
      else
        {
          UWORD c = s_pal[pen & 255];
          while(n--) *p++ = c;
        }
    }
}

static void
plot(struct RastPort *rp, LONG x, LONG y, int pen)
{
  if(x < rp->clip_x0 || x > rp->clip_x1 || y < rp->clip_y0 || y > rp->clip_y1)
    return;
  if(s_pix8)
    {
      UBYTE *p = s_pix8 + y * W + x;
      *p = (rp->DrawMode & COMPLEMENT) ? (UBYTE)(*p ^ s_xor_mask) : (UBYTE)pen;
    }
  else if(s_pix16)
    {
      UWORD *p = s_pix16 + y * W + x;
      *p = (rp->DrawMode & COMPLEMENT) ? (UWORD)(*p ^ 0x7FFF) : s_pal[pen & 255];
    }
}

/* ------------------------------------------------------------------ RastPort */

void
InitRastPort(struct RastPort *rp)
{
  memset(rp, 0, sizeof(*rp));
  rp->Mask = 0xFF;
  rp->FgPen = 1;
  rp->apen = 1;
  rp->DrawMode = JAM2;
  rp->LinePtrn = 0xFFFF;
  rp->TxHeight = 8;
  rp->TxWidth = 8;
  rp->TxBaseline = 6;
  rp->clip_x0 = 0;
  rp->clip_y0 = 0;
  rp->clip_x1 = W - 1;
  rp->clip_y1 = s_h - 1;
}

void SetAPen(struct RastPort *rp, ULONG pen) { rp->apen = (UBYTE)pen; rp->FgPen = (BYTE)pen; }
void SetBPen(struct RastPort *rp, ULONG pen) { rp->bpen = (UBYTE)pen; rp->BgPen = (BYTE)pen; }
void SetDrMd(struct RastPort *rp, ULONG mode) { rp->DrawMode = (BYTE)mode; }
void SetWriteMask(struct RastPort *rp, ULONG mask) { rp->Mask = (UBYTE)mask; }

void
SetRast(struct RastPort *rp, ULONG pen)
{
  (void)rp;
  if(s_pix8)
    fill8(s_pix8, (int)pen, (long)W * s_h);
  else if(s_pix16)
    {
      UWORD c = s_pal[pen & 255];
      UWORD *p = s_pix16;
      long n = (long)W * s_h;
      while(n--) *p++ = c;
    }
}

void Move(struct RastPort *rp, LONG x, LONG y) { rp->cp_x = (WORD)x; rp->cp_y = (WORD)y; }

/* Cohen-Sutherland outcode against the RastPort clip rectangle */
static int
outcode(struct RastPort *rp, LONG x, LONG y)
{
  int c = 0;
  if(x < rp->clip_x0) c |= 1; else if(x > rp->clip_x1) c |= 2;
  if(y < rp->clip_y0) c |= 4; else if(y > rp->clip_y1) c |= 8;
  return c;
}

/* Clip the segment to the rectangle. Returns 0 if nothing is visible.
   Clipping moves endpoints along the line; the Bresenham steps after that
   can differ by a pixel from the unclipped line, which is fine on screen. */
static int
clip_line(struct RastPort *rp, LONG *x0, LONG *y0, LONG *x1, LONG *y1)
{
  int c0 = outcode(rp, *x0, *y0), c1 = outcode(rp, *x1, *y1), k;
  for(k = 0; k < 8; k++)
    {
      LONG x = 0, y = 0;
      int c;
      if(!(c0 | c1))
        return 1;
      if(c0 & c1)
        return 0;
      c = c0 ? c0 : c1;
      if(c & 8)      { x = *x0 + (*x1 - *x0) * (rp->clip_y1 - *y0) / (*y1 - *y0); y = rp->clip_y1; }
      else if(c & 4) { x = *x0 + (*x1 - *x0) * (rp->clip_y0 - *y0) / (*y1 - *y0); y = rp->clip_y0; }
      else if(c & 2) { y = *y0 + (*y1 - *y0) * (rp->clip_x1 - *x0) / (*x1 - *x0); x = rp->clip_x1; }
      else           { y = *y0 + (*y1 - *y0) * (rp->clip_x0 - *x0) / (*x1 - *x0); x = rp->clip_x0; }
      if(c == c0) { *x0 = x; *y0 = y; c0 = outcode(rp, x, y); }
      else        { *x1 = x; *y1 = y; c1 = outcode(rp, x, y); }
    }
  return 0;
}

void
Draw(struct RastPort *rp, LONG x1, LONG y1)
{
  LONG x0 = rp->cp_x, y0 = rp->cp_y;
  LONG dx, dy, sx = 1, sy = 1, err, e2;
  int pen = rp->apen;

  rp->cp_x = (WORD)x1;
  rp->cp_y = (WORD)y1;
  if(y0 == y1)
    {
      LONG a = x0 < x1 ? x0 : x1, b = x0 < x1 ? x1 : x0, z0 = y0, z1 = y0;
      if(clip_rect(rp, &a, &z0, &b, &z1))
        span(rp, a, b, y0, pen);
      return;
    }
  if(x0 == x1 && s_pix8 && !(rp->DrawMode & COMPLEMENT))
    {
      /* vertical: clip once, then one store per row */
      LONG a = y0 < y1 ? y0 : y1, b = y0 < y1 ? y1 : y0;
      UBYTE *p;
      if(x0 < rp->clip_x0 || x0 > rp->clip_x1)
        return;
      if(a < rp->clip_y0) a = rp->clip_y0;
      if(b > rp->clip_y1) b = rp->clip_y1;
      if(a > b)
        return;
      p = s_pix8 + a * W + x0;
      for(b -= a; b >= 0; b--)
        {
          *p = (UBYTE)pen;
          p += W;
        }
      return;
    }
  if(!clip_line(rp, &x0, &y0, &x1, &y1))
    return;
  dx = x1 - x0;
  dy = y1 - y0;
  if(dx < 0) { dx = -dx; sx = -1; }
  if(dy < 0) { dy = -dy; sy = -1; }
  err = dx - dy;
  if(s_pix8 && !(rp->DrawMode & COMPLEMENT))
    {
      /* fast path: all points are inside the clip rectangle */
      UBYTE *p = s_pix8 + y0 * W + x0;
      LONG stepy = sy * W;
      for(;;)
        {
          *p = (UBYTE)pen;
          if(x0 == x1 && y0 == y1)
            break;
          e2 = err * 2;
          if(e2 > -dy) { err -= dy; x0 += sx; p += sx; }
          if(e2 <  dx) { err += dx; y0 += sy; p += stepy; }
        }
      return;
    }
  for(;;)
    {
      plot(rp, x0, y0, pen);
      if(x0 == x1 && y0 == y1)
        break;
      e2 = err * 2;
      if(e2 > -dy) { err -= dy; x0 += sx; }
      if(e2 <  dx) { err += dx; y0 += sy; }
    }
}

void
PolyDraw(struct RastPort *rp, LONG count, const WORD *xy)
{
  LONG i;
  for(i = 0; i < count; i++)
    Draw(rp, xy[2 * i], xy[2 * i + 1]);
}

void
RectFill(struct RastPort *rp, LONG x0, LONG y0, LONG x1, LONG y1)
{
  LONG y;
  /* clip inline: games call this for single font/sprite pixels thousands
   * of times a frame, so the call overhead matters */
  if(x0 < rp->clip_x0) x0 = rp->clip_x0;
  if(y0 < rp->clip_y0) y0 = rp->clip_y0;
  if(x1 > rp->clip_x1) x1 = rp->clip_x1;
  if(y1 > rp->clip_y1) y1 = rp->clip_y1;
  if(x1 < x0 || y1 < y0)
    return;
  if(s_pix8 && !(rp->DrawMode & COMPLEMENT) && x1 - x0 < 8)
    {
      /* small solid rectangle: plain byte stores, no per-row call */
      UBYTE pen = (UBYTE)rp->apen;
      UBYTE *row = s_pix8 + y0 * W + x0;
      LONG n = x1 - x0 + 1, h = y1 - y0 + 1, i;
      while(h--)
        {
          for(i = 0; i < n; i++)
            row[i] = pen;
          row += W;
        }
      return;
    }
  for(y = y0; y <= y1; y++)
    span(rp, x0, x1, y, rp->apen);
}

void
gfx_blit8(struct RastPort *rp, LONG x, LONG y, const UBYTE *img, int w, int h, int pen)
{
  LONG sx0 = 0, sy0 = 0, sx1 = w - 1, sy1 = h - 1, i, j;
  if(x < rp->clip_x0) sx0 = rp->clip_x0 - x;
  if(y < rp->clip_y0) sy0 = rp->clip_y0 - y;
  if(x + sx1 > rp->clip_x1) sx1 = rp->clip_x1 - x;
  if(y + sy1 > rp->clip_y1) sy1 = rp->clip_y1 - y;
  if(sx1 < sx0 || sy1 < sy0)
    return;
  if(s_pix8)
    {
      for(j = sy0; j <= sy1; j++)
        {
          const UBYTE *s = img + j * w;
          UBYTE *d = s_pix8 + (y + j) * W + x;
          if(pen < 0)
            {
              for(i = sx0; i <= sx1; i++)
                if(s[i]) d[i] = s[i];
            }
          else
            {
              for(i = sx0; i <= sx1; i++)
                if(s[i]) d[i] = (UBYTE)pen;
            }
        }
    }
  else
    {
      for(j = sy0; j <= sy1; j++)
        for(i = sx0; i <= sx1; i++)
          if(img[j * w + i])
            plot(rp, x + i, y + j, pen < 0 ? img[j * w + i] : pen);
    }
}

void
gfx_fill_columns(struct RastPort *rp, LONG x0, int ncols, int colw,
                 const WORD *top, LONG bottom, int pen)
{
  /* Row sweep: a column joins when the sweep reaches its top and stays to
   * `bottom`. Active columns are kept as runs (merged as neighbours join),
   * so every row is a few long spans (memset) instead of per-byte column
   * loops - the emulated ARM60 pays per instruction. */
  /* int, not WORD: ARMv3 has no halfword loads/stores, so 16-bit arrays
   * cost several instructions per access */
  static int act[GFX_WIDTH], tops[GFX_WIDTH];
  static int rs[GFX_WIDTH], re[GFX_WIDTH], idx[GFX_WIDTH];
  static int order[GFX_WIDTH], start[GFX_WIDTH + 1];
  static int rowcount[512];
  static int px0[GFX_WIDTH], plen[GFX_WIDTH];   /* each run's clipped pixels */
  LONG cx0 = rp->clip_x0, cx1 = rp->clip_x1;
  int changed = 0, nspans = 0;
  LONG y, miny = 0x7FFF, ymax;
  int i, nr = 0, nused = 0, next = 0;
  if(ncols <= 0)
    return;
  if(ncols > GFX_WIDTH)
    ncols = GFX_WIDTH;
  if(bottom > rp->clip_y1)
    bottom = rp->clip_y1;
  if(rp->DrawMode & COMPLEMENT)
    {
      /* XOR must touch each pixel once: plain per-column fills */
      for(i = 0; i < ncols; i++)
        {
          LONG t = top[i] < rp->clip_y0 ? rp->clip_y0 : top[i];
          LONG cx0 = x0 + i * colw, cx1 = cx0 + colw - 1;
          if(cx0 < rp->clip_x0) cx0 = rp->clip_x0;
          if(cx1 > rp->clip_x1) cx1 = rp->clip_x1;
          if(t <= bottom && cx0 <= cx1)
            for(y = t; y <= bottom; y++)
              span(rp, cx0, cx1, y, pen);
        }
      return;
    }
  /* bucket the visible columns by (clipped) top row */
  for(i = 0; i < ncols; i++)
    {
      LONG t = top[i] < rp->clip_y0 ? rp->clip_y0 : top[i];
      tops[i] = (int)t;
      if(t <= bottom && t < miny)
        miny = t;
    }
  if(miny > bottom)
    return;
  ymax = bottom - miny + 1;
  if(ymax > 511)
    ymax = 511;
  for(y = 0; y <= ymax; y++)
    rowcount[y] = 0;
  for(i = 0; i < ncols; i++)
    {
      LONG t = tops[i];
      act[i] = 0;
      if(t <= bottom && t - miny < ymax)
        rowcount[t - miny]++;
    }
  start[0] = 0;
  for(y = 0; y < ymax; y++)
    start[y + 1] = start[y] + rowcount[y];
  for(y = 0; y < ymax; y++)
    rowcount[y] = start[y];
  for(i = 0; i < ncols; i++)
    {
      LONG t = tops[i];
      if(t <= bottom && t - miny < ymax)
        order[rowcount[t - miny]++] = i;
    }
  nused = start[ymax];
  for(y = miny; y <= bottom; y++)
    {
      int k;
      /* columns starting on this row join the active runs */
      if(y - miny < ymax)
        for(; next < start[y - miny + 1] && next < nused; next++)
          {
            int c = order[next];
            int l = c > 0 && act[c - 1], r = c + 1 < ncols && act[c + 1];
            act[c] = 1;
            changed = 1;
            if(!l && !r)
              { rs[nr] = re[nr] = c; idx[c] = nr; nr++; }
            else if(l && !r)
              { k = idx[c - 1]; re[k] = c; idx[c] = k; }
            else if(!l && r)
              { k = idx[c + 1]; rs[k] = c; idx[c] = k; }
            else
              {
                int kl = idx[c - 1], kr = idx[c + 1], last = nr - 1;
                re[kl] = re[kr];
                idx[re[kl]] = kl;
                if(kr != last)
                  {
                    rs[kr] = rs[last];
                    re[kr] = re[last];
                    idx[rs[kr]] = kr;
                    idx[re[kr]] = kr;
                    if(kl == last)
                      kl = kr;
                  }
                nr--;
              }
          }
      if(changed)
        {
          /* runs changed: recompute their clipped pixel extents once */
          nspans = 0;
          for(k = 0; k < nr; k++)
            {
              LONG sx0 = x0 + rs[k] * colw, sx1 = x0 + (re[k] + 1) * colw - 1;
              if(sx0 < cx0) sx0 = cx0;
              if(sx1 > cx1) sx1 = cx1;
              if(sx0 <= sx1)
                {
                  px0[nspans] = (int)sx0;
                  plen[nspans] = (int)(sx1 - sx0 + 1);
                  nspans++;
                }
            }
          changed = 0;
        }
      if(s_pix8)
        {
          UBYTE *row = s_pix8 + y * W;
          for(k = 0; k < nspans; k++)
            {
              int n = plen[k];
              UBYTE *p = row + px0[k];
              if(n >= 8)
                memset(p, pen, n);
              else
                while(n--)
                  *p++ = (UBYTE)pen;
            }
        }
      else
        for(k = 0; k < nspans; k++)
          span(rp, px0[k], px0[k] + plen[k] - 1, y, pen);
    }
}

/* ---- run-length sprites ---- */
struct GfxSprite {
  int w, h, nruns;
  struct { ULONG off; UBYTE x, len, colour, y; } run[1];   /* off = y * W + x */
};

GfxSprite *
gfx_sprite_make(const UBYTE *img, int w, int h)
{
  int x, y, n = 0;
  GfxSprite *s;
  if(w > 255 || h > 255)
    return 0;
  for(y = 0; y < h; y++)
    for(x = 0; x < w; x++)
      if(img[y * w + x] && (x == 0 || img[y * w + x - 1] != img[y * w + x]))
        n++;
  s = (GfxSprite *)AllocMem(sizeof(GfxSprite) + n * sizeof(s->run[0]), MEMF_CLEAR);
  if(!s)
    return 0;
  s->w = w;
  s->h = h;
  for(y = 0; y < h; y++)
    for(x = 0; x < w; )
      {
        UBYTE c = img[y * w + x];
        int x0 = x;
        if(!c) { x++; continue; }
        while(x < w && img[y * w + x] == c)
          x++;
        s->run[s->nruns].off = (ULONG)(y * W + x0);
        s->run[s->nruns].x = (UBYTE)x0;
        s->run[s->nruns].y = (UBYTE)y;
        s->run[s->nruns].len = (UBYTE)(x - x0);
        s->run[s->nruns].colour = c;
        s->nruns++;
      }
  return s;
}

void
gfx_sprite_free(GfxSprite *s)
{
  if(s)
    FreeMem(s, sizeof(GfxSprite) + s->nruns * sizeof(s->run[0]));
}

void
gfx_sprite_draw(struct RastPort *rp, const GfxSprite *s, LONG x, LONG y, int pen)
{
  int r;
  if(!s)
    return;
  if(s_pix8 && x >= rp->clip_x0 && y >= rp->clip_y0 &&
     x + s->w - 1 <= rp->clip_x1 && y + s->h - 1 <= rp->clip_y1)
    {
      /* fully visible: no clipping per run */
      UBYTE *base = s_pix8 + y * W + x;
      for(r = 0; r < s->nruns; r++)
        {
          UBYTE *d = base + s->run[r].off;
          UBYTE c = pen < 0 ? s->run[r].colour : (UBYTE)pen;
          int n = s->run[r].len;
          while(n--)
            *d++ = c;
        }
      return;
    }
  for(r = 0; r < s->nruns; r++)
    {
      LONG ry = y + s->run[r].y, x0 = x + s->run[r].x, x1 = x0 + s->run[r].len - 1;
      if(ry < rp->clip_y0 || ry > rp->clip_y1)
        continue;
      if(x0 < rp->clip_x0) x0 = rp->clip_x0;
      if(x1 > rp->clip_x1) x1 = rp->clip_x1;
      if(x0 <= x1)
        span(rp, x0, x1, ry, pen < 0 ? s->run[r].colour : pen);
    }
}

LONG
amiga_WritePixel(struct RastPort *rp, LONG x, LONG y)
{
  if(x < rp->clip_x0 || x > rp->clip_x1 || y < rp->clip_y0 || y > rp->clip_y1)
    return -1;
  plot(rp, x, y, rp->apen);
  return 0;
}

LONG
amiga_ReadPixel(struct RastPort *rp, LONG x, LONG y)
{
  int i;
  UWORD c;
  if(x < rp->clip_x0 || x > rp->clip_x1 || y < rp->clip_y0 || y > rp->clip_y1)
    return -1;
  if(s_pix8)
    return s_pix8[y * W + x];
  c = s_pix16[y * W + x];
  for(i = 0; i < 256; i++)
    if(s_pal[i] == c)
      return i;
  return 0;
}

void
DrawEllipse(struct RastPort *rp, LONG cx, LONG cy, LONG rx, LONG ry)
{
  /* midpoint ellipse, integer */
  long x = 0, y = ry, rx2 = rx * rx, ry2 = ry * ry;
  long px = 0, py = 2 * rx2 * y, p;
  int pen = rp->apen;
  if(rx <= 0 || ry <= 0)
    {
      plot(rp, cx, cy, pen);
      return;
    }
  p = ry2 - rx2 * ry + rx2 / 4;
  while(px < py)
    {
      plot(rp, cx + x, cy + y, pen); plot(rp, cx - x, cy + y, pen);
      plot(rp, cx + x, cy - y, pen); plot(rp, cx - x, cy - y, pen);
      x++; px += 2 * ry2;
      if(p < 0) p += ry2 + px;
      else { y--; py -= 2 * rx2; p += ry2 + px - py; }
    }
  p = ry2 * (2 * x + 1) * (2 * x + 1) / 4 + rx2 * (y - 1) * (y - 1) - rx2 * ry2;
  while(y >= 0)
    {
      plot(rp, cx + x, cy + y, pen); plot(rp, cx - x, cy + y, pen);
      plot(rp, cx + x, cy - y, pen); plot(rp, cx - x, cy - y, pen);
      y--; py -= 2 * rx2;
      if(p > 0) p += rx2 - py;
      else { x++; px += 2 * ry2; p += rx2 - py + px; }
    }
}

/* ------------------------------------------------------------------ text */

LONG
Text(struct RastPort *rp, CONST_STRPTR str, ULONG count)
{
  ULONG i;
  int fg = rp->apen, bg = rp->bpen;
  int jam2 = (rp->DrawMode & JAM2) != 0;
  LONG top = rp->cp_y - rp->TxBaseline;

  if(rp->DrawMode & INVERSVID)
    {
      int t = fg; fg = bg; bg = t;
    }
  for(i = 0; i < count; i++)
    {
      unsigned c = (unsigned char)str[i];
      const unsigned char *g = font8x8_basic[c & 127];
      LONG x = rp->cp_x, row, col;
      if(c > 127)
        g = font8x8_basic[0];
      if(s_pix8 && !(rp->DrawMode & COMPLEMENT) &&
         x >= rp->clip_x0 && x + 7 <= rp->clip_x1 && top >= rp->clip_y0 && top + 7 <= rp->clip_y1)
        {
          /* fast path: whole glyph on screen, write rows directly */
          UBYTE *d = s_pix8 + top * W + x;
          if(!((ULONG)d & 3))
            {
              /* word aligned (text on 4-pixel columns): each glyph row is
               * two 32-bit stores built from expanded bit masks */
              static ULONG expand[256][2];
              static int expand_ready = 0;
              ULONG F = (ULONG)(fg & 255) * 0x01010101UL, B = (ULONG)(bg & 255) * 0x01010101UL;
              ULONG *d32 = (ULONG *)d;
              if(!expand_ready)
                {
                  int v, k;
                  for(v = 0; v < 256; v++)
                    {
                      ULONG m0 = 0, m1 = 0;
                      for(k = 0; k < 4; k++)          /* big endian: byte 0 is leftmost */
                        {
                          if((v >> k) & 1)       m0 |= 0xFFUL << (24 - 8 * k);
                          if((v >> (k + 4)) & 1) m1 |= 0xFFUL << (24 - 8 * k);
                        }
                      expand[v][0] = m0;
                      expand[v][1] = m1;
                    }
                  expand_ready = 1;
                }
              for(row = 0; row < 8; row++, d32 += W / 4)
                {
                  const ULONG *m = expand[g[row]];
                  if(jam2)
                    {
                      d32[0] = (m[0] & F) | (~m[0] & B);
                      d32[1] = (m[1] & F) | (~m[1] & B);
                    }
                  else if(g[row])
                    {
                      d32[0] = (d32[0] & ~m[0]) | (m[0] & F);
                      d32[1] = (d32[1] & ~m[1]) | (m[1] & F);
                    }
                }
              rp->cp_x = (WORD)(rp->cp_x + 8);
              continue;
            }
          for(row = 0; row < 8; row++, d += W)
            {
              unsigned bits = g[row];
              if(jam2)
                for(col = 0; col < 8; col++)
                  d[col] = (UBYTE)((bits >> col) & 1 ? fg : bg);
              else if(bits)
                for(col = 0; col < 8; col++)
                  if((bits >> col) & 1)
                    d[col] = (UBYTE)fg;
            }
          rp->cp_x = (WORD)(rp->cp_x + 8);
          continue;
        }
      for(row = 0; row < 8; row++)
        {
          unsigned bits = g[row];
          for(col = 0; col < 8; col++)
            {
              if(bits & (1u << col))         /* LSB = leftmost pixel */
                plot(rp, x + col, top + row, fg);
              else if(jam2)
                plot(rp, x + col, top + row, bg);
            }
        }
      rp->cp_x = (WORD)(rp->cp_x + 8);
    }
  return 0;
}

void
gfx_text_big(struct RastPort *rp, LONG x, LONG y, const char *str, int scale)
{
  int row, col;
  for(; *str; str++, x += 8 * scale)
    {
      const unsigned char *g = font8x8_basic[(unsigned char)*str & 127];
      for(row = 0; row < 8; row++)
        for(col = 0; col < 8; col++)
          if((g[row] >> col) & 1)
            RectFill(rp, x + col * scale, y + row * scale,
                     x + col * scale + scale - 1, y + row * scale + scale - 1);
    }
}

WORD TextLength(struct RastPort *rp, CONST_STRPTR s, ULONG count) { (void)rp; (void)s; return (WORD)(count * 8); }
LONG SetFont(struct RastPort *rp, struct TextFont *f) { (void)rp; (void)f; return 0; }
void CloseFont(struct TextFont *f) { (void)f; }
ULONG SetSoftStyle(struct RastPort *rp, ULONG s, ULONG e) { (void)rp; (void)s; (void)e; return 0; }

static struct TextFont s_topaz = { 8, 0, 0, 8, 6 };
struct TextFont *OpenFont(struct TextAttr *ta) { (void)ta; return &s_topaz; }

/* ------------------------------------------------------------------ area fill */

#define AREA_MAX 256
static WORD s_area[AREA_MAX * 2];
static int  s_area_n;

void
InitArea(struct AreaInfo *ai, APTR buffer, LONG maxvectors)
{
  ai->VctrTbl = (WORD *)buffer;
  ai->VctrPtr = (WORD *)buffer;
  ai->Count = 0;
  ai->MaxCount = (WORD)maxvectors;
}

struct TmpRas *
InitTmpRas(struct TmpRas *tr, PLANEPTR buffer, LONG size)
{
  tr->RasPtr = (BYTE *)buffer;
  tr->Size = size;
  return tr;
}

static void
area_flush(struct RastPort *rp)
{
  /* even-odd scanline fill of the polygon in s_area */
  LONG miny = 32767, maxy = -32768, y;
  int i, n = s_area_n;
  LONG xs[64];

  if(n >= 3)
    {
      for(i = 0; i < n; i++)
        {
          if(s_area[2 * i + 1] < miny) miny = s_area[2 * i + 1];
          if(s_area[2 * i + 1] > maxy) maxy = s_area[2 * i + 1];
        }
      if(miny < rp->clip_y0) miny = rp->clip_y0;
      if(maxy > rp->clip_y1) maxy = rp->clip_y1;
      for(y = miny; y <= maxy; y++)
        {
          int k = 0, a, b;
          for(i = 0; i < n; i++)
            {
              LONG x0 = s_area[2 * i], y0 = s_area[2 * i + 1];
              LONG x1 = s_area[2 * ((i + 1) % n)], y1 = s_area[2 * ((i + 1) % n) + 1];
              if((y0 <= y && y1 > y) || (y1 <= y && y0 > y))
                if(k < 64)
                  xs[k++] = x0 + ((y - y0) * (x1 - x0)) / (y1 - y0);
            }
          /* insertion sort */
          for(a = 1; a < k; a++)
            {
              LONG v = xs[a];
              for(b = a - 1; b >= 0 && xs[b] > v; b--)
                xs[b + 1] = xs[b];
              xs[b + 1] = v;
            }
          for(a = 0; a + 1 < k; a += 2)
            {
              LONG x0 = xs[a], x1 = xs[a + 1], yy0 = y, yy1 = y;
              if(clip_rect(rp, &x0, &yy0, &x1, &yy1))
                span(rp, x0, x1, y, rp->apen);
            }
        }
    }
  s_area_n = 0;
}

LONG
AreaMove(struct RastPort *rp, LONG x, LONG y)
{
  if(s_area_n >= 3)
    area_flush(rp);
  s_area_n = 0;
  s_area[0] = (WORD)x;
  s_area[1] = (WORD)y;
  s_area_n = 1;
  return 0;
}

LONG
AreaDraw(struct RastPort *rp, LONG x, LONG y)
{
  (void)rp;
  if(s_area_n >= AREA_MAX)
    return -1;
  s_area[2 * s_area_n] = (WORD)x;
  s_area[2 * s_area_n + 1] = (WORD)y;
  s_area_n++;
  return 0;
}

LONG
AreaEnd(struct RastPort *rp)
{
  area_flush(rp);
  return 0;
}

LONG
AreaEllipse(struct RastPort *rp, LONG cx, LONG cy, LONG rx, LONG ry)
{
  LONG y;
  for(y = -ry; y <= ry; y++)
    {
      /* x extent: rx * sqrt(1 - y^2/ry^2), integer */
      LONG t = ry ? (ry * ry - y * y) * rx * rx / (ry * ry) : 0, x = 0, x0, x1, yy0, yy1;
      while((x + 1) * (x + 1) <= t) x++;
      x0 = cx - x; x1 = cx + x; yy0 = yy1 = cy + y;
      if(clip_rect(rp, &x0, &yy0, &x1, &yy1))
        span(rp, x0, x1, cy + y, rp->apen);
    }
  return 0;
}

/* ------------------------------------------------------------------ bitmaps */

void
InitBitMap(struct BitMap *bm, LONG depth, LONG width, LONG height)
{
  memset(bm, 0, sizeof(*bm));
  bm->Depth = (UBYTE)depth;
  bm->BytesPerRow = (UWORD)(((width + 15) >> 4) << 1);
  bm->Rows = (UWORD)height;
}

PLANEPTR AllocRaster(ULONG w, ULONG h) { return (PLANEPTR)calloc(1, ((w + 15) >> 4) * 2 * h); }
void FreeRaster(PLANEPTR p, ULONG w, ULONG h) { (void)w; (void)h; free(p); }

void
BltBitMapRastPort(const struct BitMap *src, LONG sx, LONG sy, struct RastPort *rp,
                  LONG dx, LONG dy, LONG w, LONG h, ULONG minterm)
{
  LONG x, y;
  int d;
  (void)minterm;
  for(y = 0; y < h; y++)
    for(x = 0; x < w; x++)
      {
        LONG bx = sx + x, by = sy + y;
        int pen = 0;
        for(d = 0; d < src->Depth && d < 8; d++)
          if(src->Planes[d] &&
             (src->Planes[d][by * src->BytesPerRow + (bx >> 3)] & (0x80 >> (bx & 7))))
            pen |= 1 << d;
        plot(rp, dx + x, dy + y, pen);
      }
}

void
ScrollRaster(struct RastPort *rp, LONG dx, LONG dy, LONG x0, LONG y0, LONG x1, LONG y1)
{
  /* move the rectangle's contents by (-dx,-dy), fill the exposed part with BgPen */
  LONG x, y;
  if(!clip_rect(rp, &x0, &y0, &x1, &y1) || !s_pix8)
    return;
  if(dy >= 0)
    for(y = y0; y <= y1; y++)
      for(x = (dx >= 0 ? x0 : x1); dx >= 0 ? x <= x1 : x >= x0; x += (dx >= 0 ? 1 : -1))
        {
          LONG fx = x + dx, fy = y + dy;
          s_pix8[y * W + x] = (fx >= x0 && fx <= x1 && fy >= y0 && fy <= y1)
                              ? s_pix8[fy * W + fx] : rp->bpen;
        }
  else
    for(y = y1; y >= y0; y--)
      for(x = (dx >= 0 ? x0 : x1); dx >= 0 ? x <= x1 : x >= x0; x += (dx >= 0 ? 1 : -1))
        {
          LONG fx = x + dx, fy = y + dy;
          s_pix8[y * W + x] = (fx >= x0 && fx <= x1 && fy >= y0 && fy <= y1)
                              ? s_pix8[fy * W + fx] : rp->bpen;
        }
}
