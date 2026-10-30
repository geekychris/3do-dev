// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_draw.h - the shared engine's drawing, the same calls on both
 * Amigas: on the classic Amiga a Canvas is a RastPort on a five-bitplane
 * screen and the CPU writes the spans straight into the planes; on
 * AmigaOS 4 it is a 320x256 buffer of 8-bit pens that the platform turns
 * into RGB. 32 pens either way.
 *
 * 3D: polygons in camera space go into a draw list in depth buckets and
 * are drawn far to near (a painter's algorithm); within a bucket, in the
 * order they were added. Models are built from boxes, pyramids and prisms
 * and drawn at any position and rotation, their faces sorted by depth.
 */
#ifndef A3D_DRAW_H
#define A3D_DRAW_H

#include "a3d.h"

#ifdef __PPC__
typedef struct { void *pix; int w, h, bpp; } Canvas; /* w: the row in pixels; bpp: 4 or 2 bytes */
/* the pens' colours (plat_palette), and a pen that is a gradient down the
 * rows (plat_sky): the engine looks them up as it draws */
extern uint32_t a3_lut[32];
extern int a3_sky_pen, a3_sky_rows;
extern uint32_t a3_sky_col[PHYS_H];
extern int a3_order;                    /* the canvas's format: 0 A R G B, 1 B G R A, 2 R G B A (32-bit), 3 R5G6B5, 4 R5G6B5 little-endian */
#define A3_BPP(order) ((order) >= 3 ? 2 : 4)
#elif defined(AMIGA3DO)
/* the 3DO: the sdk/amiga layer's 320 x 256 frame of 8-bit pens (or the
 * tall picture); after an a3_flush the frame's canvas draws into a 16-bit
 * overlay (bpp 2) over the 3D, which the cel engine draws (a3d_draw.c) */
typedef struct { uint8_t *pix; int w, h, bpp; } Canvas;
void a3_frame_begin(Canvas *cv);        /* main_3do.c, before game_draw: cv is the frame */
void a3_frame_end(void);                /* after it: the 3D and the overlays to the layer */
void a3_set_sky(int pen, const uint16_t *rows15, int stride);   /* plat_sky's gradient, for the overlays */
/* a game's own sorted polygon list through the cel engine (as a3_flush
 * does with the draw list): 0 from begin = draw them the usual way */
int  a3_cels_begin(Canvas *cv);
void a3_cels_poly(const int16_t *xs, const int16_t *ys, int n, int col);   /* the drawing's pixels */
void a3_cels_end(void);
#else
#include <graphics/rastport.h>
typedef struct RastPort Canvas;
#endif

int  a3_draw_init(void);                /* loads topaz 8 for the text */

/* ---------- 2D, straight onto the canvas ---------- */
/* the rows the 2D calls draw in, 0..SCREEN_H-1 unless a game narrows it
 * (or widens it for a tall picture, below) */
extern int g_clip_y0, g_clip_y1;

void g_span(Canvas *cv, int y, int xl, int xr, int col);         /* no clipping */
void g_rect(Canvas *cv, int x0, int y0, int x1, int y1, int col);
void g_fill(Canvas *cv, int x0, int y0, int x1, int y1, int col);   /* big areas (the blitter on 68k) */
void g_sync(void);                      /* after g_fill, before the CPU draws there */
void g_hline(Canvas *cv, int x0, int x1, int y, int col);
void g_vline(Canvas *cv, int x, int y0, int y1, int col);
void g_poly(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col, int y0, int y1);
/* a polygon in the drawing's own pixels (x A3_SUB): finer on AmigaOS 4 */
void g_poly_sub(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col);
/* a span in the drawing's own pixels, clipped (to the clip rows and the frame) */
void g_span_sub(Canvas *cv, int py, int xl, int xr, int col);
/* the same, see-through: alpha 0..256 of the pen over what is there (on the
 * classic Amiga a dither: every other pixel from 1/4 to 3/4) */
void g_blend_sub(Canvas *cv, int py, int xl, int xr, int col, int alpha);
void g_circle(Canvas *cv, int cx, int cy, int r, int col);       /* filled */
void g_text(Canvas *cv, int x, int y, int col, const char *s);  /* y: the baseline */
void g_text_shadow(Canvas *cv, int x, int y, int col, const char *s);
void g_text_centre(Canvas *cv, int y, int col, const char *s);   /* shadowed */
void g_text_big(Canvas *cv, int x, int y, int col, const char *s, int scale);  /* y: the top */
void g_box(Canvas *cv, int x0, int y0, int x1, int y1, int fill, int edge);
const uint8_t *g_glyph_bits(int c);     /* topaz 8: 8 rows, bit 7 left */

/* ---------- a tall picture ----------
 * Drawn once (a fixed scene: a pinball table), then copied into the
 * canvas a window of rows at a time each frame - the blitter on the
 * classic Amiga, memcpy on AmigaOS 4. The 2D calls draw into it (set
 * g_clip_y1 to its height - 1 first). */
Canvas *g_pic_alloc(int h);             /* SCREEN_W x h, cleared to pen 0; NULL: no memory */
void g_pic_blit(Canvas *cv, int src_y, int dst_y, int rows);
void g_pic_free(void);

/* ---------- pictures (AmigaOS 4) ----------
 * Full-colour images made by tools/make_image.py ("A3IM", RGB), drawn at the
 * finer resolution: a 640 x 512 one fills the screen. The classic Amiga has
 * 32 colours: there g_image_load gives NULL, and games keep their own look. */
typedef struct A3Image A3Image;
A3Image *g_image_load(const char *path);
void g_image_draw(Canvas *cv, A3Image *im, int x, int y);   /* x, y: the games' coordinates */
void g_image_free(A3Image *im);
/* part of one row of a picture: its columns x0..x1 of row sy, to the
 * drawing's pixels (dx + x0, dy) - for pictures shaped by a mask */
void g_image_row(Canvas *cv, A3Image *im, int sy, int x0, int x1, int dx, int dy);
/* AmigaOS 4: rows y0..y1 (the games' coordinates) across the whole width,
 * shading from R G B top to bottom (the classic Amiga: nothing) */
void g_fill_grad(Canvas *cv, int y0, int y1, uint32_t top, uint32_t bottom);
/* a backdrop that wraps round: the drawing's rows y0..y1 across the whole
 * width, row y from the picture's row y - top (its first and last rows
 * carried on above and below it), starting at its column sx (any value,
 * wrapped) - a sky panorama that turns with the camera */
void g_image_wrap(Canvas *cv, A3Image *im, int sx, int top, int y0, int y1);
int  g_image_w(const A3Image *im);      /* in the drawing's pixels; 0 for NULL */
int  g_image_h(const A3Image *im);

/* ---------- 3D draw list ---------- */
void a3_begin(int32_t far_z);           /* empty the list; depths 0..far_z are bucketed */
void a3_poly(const Cam *c, int32_t depth, int col, V3 *pts, int n);   /* camera space */
void a3_quad(const Cam *c, int32_t depth, int col, V3 a, V3 b, V3 d, V3 e);
void a3_flush(Canvas *cv);              /* draw the list, far to near */
extern int32_t a3_npolys, a3_dropped;

/* ---------- models ---------- (a game with its own defines A3D_NO_MODELS) */
#ifndef A3D_NO_MODELS
#define MDL_MAXV 64
#define MDL_MAXF 48
#define COL_BODY  200                   /* face colour: the body pens given when drawing (+1 shaded) */
#define COL_FLASH 210                   /* face colour: flashing */

typedef struct { uint8_t n, col; uint8_t idx[4]; } Face;
typedef struct {
    int nv, nf;
    int16_t v[MDL_MAXV][3];
    Face f[MDL_MAXF];
} Model;

void mdl_begin(Model *m);
void mdl_box(Model *m, int lit, int shade, int x0, int x1, int y0, int y1, int z0, int z1);
void mdl_pyramid(Model *m, int lit, int shade, int x0, int x1, int y0, int z0, int z1, int apex);
void mdl_prism(Model *m, int lit, int shade, int sides, int r, int y0, int y1);   /* about the y axis */
void mdl_shadow(Model *m, int col, int x0, int x1, int z0, int z1);   /* flat at y = 1, always first */
void mdl_face(Model *m, int lit, int shade, int n, int a, int b, int c, int d, int ix, int iy, int iz);
int  mdl_vtx(Model *m, int x, int y, int z);

/* rotation matrices, 16.16, row-major: world = M * model */
void a3_rot_yaw(int32_t m[9], int yaw);
void a3_rot_ypr(int32_t m[9], int yaw, int pitch, int roll);
/* draw a model; rot NULL = unrotated; body: COL_BODY's lit and shaded pens;
 * flash: which pen COL_FLASH faces take; depth_bias moves it in the list */
void a3_model(const Cam *c, const Model *m, int32_t x, int32_t y, int32_t z, const int32_t *rot,
              const uint8_t *body, int flash, int32_t depth_bias);
#endif

#endif
