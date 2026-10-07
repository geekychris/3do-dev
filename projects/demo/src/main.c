/*
  CLAUDE x 3DO  -  a small cel-engine demo for the 3DO Interactive Multiplayer

  Everything on screen is a cel; one DrawCels() call per frame renders a
  single linked list:

    plasma   64x48 16bpp cel, recomputed in software every frame, stretched
             5x to fill the screen by the cel engine (80x60 drops to 30fps)
    stars    3D starfield; brightness comes from each cel's PIXC multiplier
    vortex   hollow squares (1-texel border, transparent middle), rotated
             and zoomed with HDX/HDY/VDX/VDY; colour cycling is just
             swapping ccb_SourcePtr between pre-coloured pixel buffers
    title    per-letter cels bouncing on a sine wave
    scroller per-letter cels on a sine path

  Controls:
    A          next plasma palette
    B          toggle vortex
    C          toggle starfield
    UP/DOWN    plasma speed
    LEFT/RIGHT vortex spin
    L/R        font style
    P (start)  pause
    X (stop)   quit

  Telemetry: lines starting with "DEMO:" go to the debug console (kprintf),
  which the tdo harness captures. tests/demo_test.py relies on them.
*/
#include "demo.h"
#include "tdo.h"

/* ------------------------------------------------------------------ config */
#ifndef PLASMA_W                 /* override: make EXTRA_CFLAGS=-DPLASMA_W=.. */
#define PLASMA_W      64
#define PLASMA_H      48
#define PLASMA_SCALE  5           /* 64*5 = 320, 48*5 = 240 */
#endif
#define NUM_PALETTES  4
#define NUM_SQUARES   12
#define SQUARE_SIZE   32
#define NUM_STARS     48
#define STAR_DEPTH    1024
#define TITLE_TEXT    "CLAUDE X 3DO"
#define TITLE_LEN     12
#define TITLE_SCALE   3
#define SCROLL_SLOTS  21
#define SCROLL_SCALE  2
#define STATUS_EVERY  300         /* frames between DEMO: status lines */

static const char SCROLL_TEXT[] =
  "     HELLO FROM CLAUDE!  THIS DEMO WAS WRITTEN, BUILT AND TESTED BY AN AI "
  "USING THE 3DO DEVKIT, NORCROFT ARM C AND A SCRIPTABLE OPERA EMULATOR.  "
  "EVERYTHING YOU SEE IS A CEL...  A: PALETTE  B: VORTEX  C: STARS  "
  "UP/DOWN: SPEED  L/R: FONT  X: QUIT  ...  GREETINGS GINO AND RJ MICAL!  "
  "...  GREETINGS TO TRAPEXIT, OPTIMUS AND THE 3DODEV.COM COMMUNITY!          ";

/* PIXC words (both halves identical so the pixel P bit doesn't matter) */
#define PIXC_HALF(mf)   (PPMPC_1S_PDC | PPMPC_MS_CCB | (mf) | PPMPC_SF_8 | PPMPC_2S_0 | PPMPC_2D_1)
#define PIXC_BOTH(h)    (((u32)(h) << 16) | (u32)(h))

/* ------------------------------------------------------------------ state */
static ScreenContext s_sc;
static Item          s_vbl;

static u8   s_sin[256];               /* 0..63 sine for plasma terms */
static s32  s_sinf[256];              /* -256..256 sine (8.8 fixed)  */
static u16  s_pal[NUM_PALETTES][256];

static CCB *s_plasma;
static CCB *s_squares[NUM_SQUARES];
static u16 *s_square_data[NUM_SQUARES];
static CCB *s_stars[NUM_STARS];
static s32  s_star_x[NUM_STARS], s_star_y[NUM_STARS], s_star_z[NUM_STARS];
static u16 *s_star_pixels;
static CCB *s_title[TITLE_LEN];
static CCB *s_scroll[SCROLL_SLOTS];
static void *s_blank_glyph;

static s32 s_palette     = 0;
static s32 s_speed       = 2;
static s32 s_spin        = 1;
static s32 s_font        = 0;
static s32 s_show_vortex = 1;
static s32 s_show_stars  = 1;
static s32 s_paused      = 0;

/* ------------------------------------------------------------------ helpers */
static
u16
rgb15(s32 r_, s32 g_, s32 b_)
{
  if(r_ < 0) r_ = 0; if(r_ > 31) r_ = 31;
  if(g_ < 0) g_ = 0; if(g_ > 31) g_ = 31;
  if(b_ < 0) b_ = 0; if(b_ > 31) b_ = 31;
  /* never emit 0: that's transparent in an uncoded cel */
  if((r_ | g_ | b_) == 0) b_ = 1;
  return (u16)MakeRGB15(r_,g_,b_);
}

static
void
build_tables(void)
{
  s32 i;

  for(i = 0; i < 256; i++)
    {
      frac16 s = SinF16(Convert32_F16(i));       /* 256 units per circle */
      s_sinf[i] = s >> 8;                        /* 8.8 fixed */
      s_sin[i]  = (u8)((s + 65536) * 63 / 131072);
    }

  for(i = 0; i < 256; i++)
    {
      s32 a = s_sinf[i & 255];                   /* -256..256 */
      s32 b = s_sinf[(i * 2 + 64) & 255];
      s32 c = s_sinf[(i + 85) & 255];
      s32 d = s_sinf[(i + 170) & 255];

      /* 0: Claude - coral, cream and deep plum */
      s_pal[0][i] = rgb15(20 + (a * 11 >> 8), 11 + (b * 8 >> 8), 9 + (c * 8 >> 8));
      /* 1: fire */
      s_pal[1][i] = rgb15(18 + (a * 14 >> 8), 8 + (a * 9 >> 8) + (b * 3 >> 8), 2 + (b * 2 >> 8));
      /* 2: ocean */
      s_pal[2][i] = rgb15(3 + (b * 3 >> 8), 14 + (a * 10 >> 8), 20 + (c * 11 >> 8));
      /* 3: rainbow */
      s_pal[3][i] = rgb15(16 + (a * 15 >> 8), 16 + (c * 15 >> 8), 16 + (d * 15 >> 8));
    }
}

static
CCB *
new_cel(s32 w_, s32 h_, void *data_)
{
  CCB *c = CreateCel(w_, h_, 16, CREATECEL_UNCODED, data_);
  if(c == NULL)
    {
      kprintf("DEMO: FATAL CreateCel(%d,%d) failed\n", w_, h_);
      return NULL;
    }
  return c;
}

/* Set a cel's 2x2 mapping matrix: rotate by angle (256/circle), uniform zoom
   (16.16) and centre it on (cx,cy) in pixels. */
static
void
place_rotated(CCB *c_, s32 cx_, s32 cy_, frac16 zoom_, frac16 angle_)
{
  s32 hdx = MulSF16(CosF16(angle_), zoom_);
  s32 hdy = MulSF16(SinF16(angle_), zoom_);
  s32 vdx = -hdy;
  s32 vdy =  hdx;
  s32 hw  = c_->ccb_Width  >> 1;
  s32 hh  = c_->ccb_Height >> 1;

  c_->ccb_HDX = hdx << 4;   /* 12.20 */
  c_->ccb_HDY = hdy << 4;
  c_->ccb_VDX = vdx;        /* 16.16 */
  c_->ccb_VDY = vdy;
  c_->ccb_XPos = Convert32_F16(cx_) - (hdx * hw) - (vdx * hh);
  c_->ccb_YPos = Convert32_F16(cy_) - (hdy * hw) - (vdy * hh);
}

static
void
place_scaled(CCB *c_, s32 x_, s32 y_, s32 scale_)
{
  c_->ccb_XPos = Convert32_F16(x_);
  c_->ccb_YPos = Convert32_F16(y_);
  c_->ccb_HDX  = scale_ << 20;
  c_->ccb_HDY  = 0;
  c_->ccb_VDX  = 0;
  c_->ccb_VDY  = scale_ << 16;
}

static
void
link_all(CCB **list_, s32 n_, CCB ***tail_)
{
  s32 i;
  for(i = 0; i < n_; i++)
    {
      **tail_ = list_[i];
      *tail_  = &list_[i]->ccb_NextPtr;
    }
}

/* ------------------------------------------------------------------ setup */
static
Err
create_scene(void)
{
  s32 i, j, k;

  /* plasma */
  s_plasma = new_cel(PLASMA_W, PLASMA_H, NULL);
  if(s_plasma == NULL) return -1;
  place_scaled(s_plasma, 0, 0, PLASMA_SCALE);

  /* stars: all share one 2x2 white pixel buffer */
  s_star_pixels = (u16*)AllocMem(2 * 2 * 2 * 2, MEMTYPE_CEL); /* rows padded to 2 words */
  if(s_star_pixels == NULL) return -1;
  for(i = 0; i < 8; i++)
    s_star_pixels[i] = (u16)MakeRGB15(31,31,31);
  for(i = 0; i < NUM_STARS; i++)
    {
      s_stars[i] = new_cel(2, 2, s_star_pixels);
      if(s_stars[i] == NULL) return -1;
      s_star_x[i] = (s32)(ReadHardwareRandomNumber() % 640) - 320;
      s_star_y[i] = (s32)(ReadHardwareRandomNumber() % 480) - 240;
      s_star_z[i] = (s32)(ReadHardwareRandomNumber() % STAR_DEPTH) + 1;
    }

  /* vortex: hollow squares, each a different hue so cycling = pointer swap */
  for(i = 0; i < NUM_SQUARES; i++)
    {
      s_squares[i] = new_cel(SQUARE_SIZE, SQUARE_SIZE, NULL);
      if(s_squares[i] == NULL) return -1;
      s_square_data[i] = (u16*)s_squares[i]->ccb_SourcePtr;
      for(j = 0; j < SQUARE_SIZE; j++)
        for(k = 0; k < SQUARE_SIZE; k++)
          {
            s32 edge = (j == 0 || k == 0 || j == SQUARE_SIZE - 1 || k == SQUARE_SIZE - 1);
            s32 h    = (i * 256) / NUM_SQUARES;
            s_square_data[i][(j * SQUARE_SIZE) + k] =
              edge ? rgb15(18 + (s_sinf[h & 255] * 13 >> 8),
                           18 + (s_sinf[(h + 85) & 255] * 13 >> 8),
                           18 + (s_sinf[(h + 170) & 255] * 13 >> 8))
                   : 0;
          }
    }

  /* text */
  s_blank_glyph = font_glyph(' ', 0);
  for(i = 0; i < TITLE_LEN; i++)
    {
      s_title[i] = new_cel(8, 8, font_glyph(TITLE_TEXT[i], 0));
      if(s_title[i] == NULL) return -1;
    }
  for(i = 0; i < SCROLL_SLOTS; i++)
    {
      s_scroll[i] = new_cel(8, 8, s_blank_glyph);
      if(s_scroll[i] == NULL) return -1;
    }

  return 0;
}

/* Re-link the draw list (cheap; done every frame so toggles just work). */
static
CCB *
build_draw_list(void)
{
  CCB  *head = NULL;
  CCB **tail = &head;
  CCB  *last;

  link_all(&s_plasma, 1, &tail);
  if(s_show_stars)
    link_all(s_stars, NUM_STARS, &tail);
  if(s_show_vortex)
    link_all(s_squares, NUM_SQUARES, &tail);
  link_all(s_title, TITLE_LEN, &tail);
  link_all(s_scroll, SCROLL_SLOTS, &tail);
  *tail = NULL;

  /* CCB_LAST on the final cel only */
  for(last = head; last != NULL; last = last->ccb_NextPtr)
    {
      if(last->ccb_NextPtr == NULL)
        last->ccb_Flags |= CCB_LAST;
      else
        last->ccb_Flags &= ~CCB_LAST;
    }

  return head;
}

/* ------------------------------------------------------------------ per frame */
static
void
update_plasma(s32 t_)
{
  u16 *dst = (u16*)s_plasma->ccb_SourcePtr;
  const u16 *pal = s_pal[s_palette];
  u8  colterm[PLASMA_W];
  s32 x, y;
  s32 t1 = t_;
  s32 t2 = t_ * 2;
  s32 t3 = t_ * 3;

  for(x = 0; x < PLASMA_W; x++)
    colterm[x] = s_sin[(x * 5 + t1) & 255];

  for(y = 0; y < PLASMA_H; y++)
    {
      s32 rowterm = s_sin[(y * 6 - t2) & 255];
      s32 diag    = y * 3 + t3;
      s32 shift   = rowterm + t1;
      for(x = 0; x < PLASMA_W; x++)
        {
          s32 v = colterm[x] + shift + s_sin[(x * 2 + diag) & 255] + s_sin[(x + y * 4 + t2) & 255];
          *dst++ = pal[v & 255];
        }
    }
}

static
void
update_stars(s32 speed_)
{
  s32 i;
  for(i = 0; i < NUM_STARS; i++)
    {
      s32 z, sx, sy, b;
      s_star_z[i] -= 4 + speed_ * 3;
      if(s_star_z[i] <= 1)
        {
          s_star_x[i] = (s32)(ReadHardwareRandomNumber() % 640) - 320;
          s_star_y[i] = (s32)(ReadHardwareRandomNumber() % 480) - 240;
          s_star_z[i] = STAR_DEPTH;
        }
      z  = s_star_z[i];
      sx = (SCREEN_W / 2) + ((s_star_x[i] * 160) / z);
      sy = (SCREEN_H / 2) + ((s_star_y[i] * 160) / z);
      if(sx < 0 || sx >= SCREEN_W || sy < 0 || sy >= SCREEN_H)
        {
          s_stars[i]->ccb_Flags |= CCB_SKIP;
          continue;
        }
      s_stars[i]->ccb_Flags &= ~CCB_SKIP;
      place_scaled(s_stars[i], sx, sy, (z < 256) ? 2 : 1);
      /* brightness 1/8 .. 8/8 via the multiply factor */
      b = 7 - ((z * 7) / STAR_DEPTH);
      s_stars[i]->ccb_PIXC = PIXC_BOTH(PIXC_HALF((u32)b << PPMPC_MF_SHIFT));
    }
}

static
void
update_vortex(s32 t_)
{
  s32 i;
  for(i = 0; i < NUM_SQUARES; i++)
    {
      s32    phase = t_ * 2 + i * 9;
      s32    cx    = (SCREEN_W / 2) + ((s_sinf[(t_ + i * 6) & 255] * 40) >> 8);
      s32    cy    = (SCREEN_H / 2) + ((s_sinf[(t_ * 2 + i * 6 + 64) & 255] * 24) >> 8);
      frac16 zoom  = (Convert32_F16(1 + i) >> 1) + (s_sinf[phase & 255] << 6);
      frac16 angle = Convert32_F16((s_spin * (t_ + i * 8)) & 255);

      place_rotated(s_squares[i], cx, cy, zoom, angle);
      /* colour cycling: rotate which hue each ring uses */
      s_squares[i]->ccb_SourcePtr = (CelData*)s_square_data[(i + (t_ >> 3)) % NUM_SQUARES];
    }
}

static
void
update_text(s32 t_)
{
  s32 i;
  s32 title_x = (SCREEN_W - (TITLE_LEN * 8 * TITLE_SCALE)) / 2;
  s32 scroll_len = (s32)(sizeof(SCROLL_TEXT) - 1);
  s32 pos  = t_ * 2;                  /* pixels scrolled */
  s32 first = pos / (8 * SCROLL_SCALE);
  s32 sub   = pos % (8 * SCROLL_SCALE);

  for(i = 0; i < TITLE_LEN; i++)
    {
      s32 bounce = (s_sinf[(t_ * 4 + i * 20) & 255] * 10) >> 8;
      if(bounce > 0) bounce = -bounce;  /* bounce off the top line */
      s_title[i]->ccb_SourcePtr = (CelData*)font_glyph(TITLE_TEXT[i], s_font);
      place_scaled(s_title[i], title_x + (i * 8 * TITLE_SCALE), 26 + bounce, TITLE_SCALE);
    }

  for(i = 0; i < SCROLL_SLOTS; i++)
    {
      s32 x = (i * 8 * SCROLL_SCALE) - sub;
      s32 y = 196 + ((s_sinf[(x + t_ * 3) & 255] * 12) >> 8);
      char c = SCROLL_TEXT[(first + i) % scroll_len];
      s_scroll[i]->ccb_SourcePtr = (CelData*)font_glyph(c, s_font ^ 1);
      place_scaled(s_scroll[i], x, y, SCROLL_SCALE);
    }
}

/* ------------------------------------------------------------------ input */
static
s32
handle_input(void)
{
  u32 buttons = 0;
  Err err = DoControlPad(1, &buttons, ControlUp | ControlDown);
  if(err < 0)
    return 0;

  if(buttons & ControlX)
    return -1;
  if(buttons & ControlA)
    {
      s_palette = (s_palette + 1) % NUM_PALETTES;
      kprintf("DEMO: input A palette=%d\n", s_palette);
    }
  if(buttons & ControlB)
    {
      s_show_vortex = !s_show_vortex;
      kprintf("DEMO: input B vortex=%d\n", s_show_vortex);
    }
  if(buttons & ControlC)
    {
      s_show_stars = !s_show_stars;
      kprintf("DEMO: input C stars=%d\n", s_show_stars);
    }
  if(buttons & ControlStart)
    {
      s_paused = !s_paused;
      kprintf("DEMO: input P paused=%d\n", s_paused);
    }
  if(buttons & (ControlLeftShift | ControlRightShift))
    {
      s_font ^= 1;
      kprintf("DEMO: input L/R font=%d\n", s_font);
    }
  if(buttons & ControlLeft)  { s_spin = -1; kprintf("DEMO: input LEFT spin=-1\n"); }
  if(buttons & ControlRight) { s_spin =  1; kprintf("DEMO: input RIGHT spin=1\n"); }
  if((buttons & ControlUp) && s_speed < 8)   s_speed++;
  if((buttons & ControlDown) && s_speed > 0) s_speed--;

  return 0;
}

/* ------------------------------------------------------------------ main */
int
main(void)
{
  Err err;
  s32 frame = 0;
  s32 t     = 0;
  s32 cur   = 0;

  kprintf("DEMO: boot\n");

  if(OpenGraphicsFolio() < 0 || OpenMathFolio() < 0)
    {
      kprintf("DEMO: FATAL folio open failed\n");
      return 1;
    }
  if(InitControlPad(1) < 0)
    {
      kprintf("DEMO: FATAL InitControlPad failed\n");
      return 1;
    }
  if(CreateBasicDisplay(&s_sc, DI_TYPE_DEFAULT, 2) < 0)
    {
      kprintf("DEMO: FATAL CreateBasicDisplay failed\n");
      return 1;
    }
  for(cur = 0; cur < 2; cur++)
    {
      DisableHAVG(s_sc.sc_Screens[cur]);
      DisableVAVG(s_sc.sc_Screens[cur]);
    }
  cur   = 0;
  s_vbl = GetVBLIOReq();

  build_tables();
  if(font_init() < 0 || create_scene() < 0)
    {
      kprintf("DEMO: FATAL out of memory\n");
      return 1;
    }

  tdo_log("DEMO: ready %dx%d plasma=%dx%d stars=%d squares=%d\n",
          s_sc.sc_BitmapWidth, s_sc.sc_BitmapHeight,
          PLASMA_W, PLASMA_H, NUM_STARS, NUM_SQUARES);

  for(;;)
    {
      if(handle_input() < 0)
        break;

      if(!s_paused)
        {
          t += s_speed;
          update_plasma(t);
          if(s_show_stars)  update_stars(s_speed);
          if(s_show_vortex) update_vortex(t);
          update_text(t);
        }

      err = DrawCels(s_sc.sc_BitmapItems[cur], build_draw_list());
      if(err < 0 && (frame % STATUS_EVERY) == 0)
        kprintf("DEMO: DrawCels err=%d\n", err);

      DisplayScreen(s_sc.sc_Screens[cur], 0);
      cur = !cur;
      WaitVBL(s_vbl, 1);

      frame++;
      if((frame % STATUS_EVERY) == 0)
        tdo_log("DEMO: status frame=%d t=%d palette=%d speed=%d vortex=%d stars=%d\n",
                frame, t, s_palette, s_speed, s_show_vortex, s_show_stars);
    }

  kprintf("DEMO: exit frame=%d\n", frame);
  KillControlPad();
  DeleteBasicDisplay(&s_sc);
  return 0;
}
