/*
 * amiga3do.h - the 3DO side of the Amiga compatibility layer.
 *
 * Ported games keep their game.c / draw.c (which call the Amiga graphics
 * API: SetAPen, RectFill, Move/Draw, Text, ...) and replace main.c with a
 * small main_3do.c that uses the functions below. See sdk/amiga/README.md.
 *
 * Rendering model
 *   The game draws into an offscreen 320 x height (default 256) buffer,
 *   exactly like an Amiga low-res screen. gfx_swap() shows it through one
 *   hardware cel, scaled to the 3DO's 320x240 (or cropped, see
 *   gfx_set_view). GFX_PAL32 mode stores 8-bit pens and uses the cel
 *   engine's 32-entry palette, so palette changes recolour the whole screen
 *   instantly, like Amiga colour registers. GFX_RGB16 mode (for 256-colour
 *   AGA games) converts pens to 15-bit colour as it draws.
 *
 * Timing
 *   gfx_swap() paces the game to 50 frames per second (PAL Amiga speed) on
 *   the 60 Hz 3DO by showing 5 frames per 6 vertical blanks.
 *   gfx_set_rate(60) runs one frame per VBL instead.
 *
 * Input
 *   pad_held(port) / pad_pressed(port) return PAD_* bits. By convention the
 *   X (stop) button quits back to the menu: check amiga_quit_requested().
 */
#ifndef AMIGA3DO_H
#define AMIGA3DO_H

#include "amiga_types.h"
#include "amiga_compat.h"     /* AllocMem, files, amiga_load_file */
#include <graphics/rastport.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_WIDTH      320

#define GFX_PAL32      0     /* 8-bit pens, 32-entry hardware palette (OCS/ECS games) */
#define GFX_RGB16      1     /* 16-bit pixels, 256-entry software palette (AGA games) */

#define GFX_VIEW_SCALE 0     /* scale the logical height onto 240 lines (default) */
#define GFX_VIEW_CROP  1     /* show 240 lines starting at y0, 1:1 */

/* ---- graphics ---- */
int   gfx_init(const UWORD *rgb4_palette, int ncolors);      /* GFX_PAL32, 320x256 */
int   gfx_init_mode(int mode, int height, const UWORD *rgb4_palette, int ncolors);
void  gfx_exit(void);
struct RastPort *gfx_back(void);       /* the RastPort to draw into */
void  gfx_swap(void);                  /* show the frame, pace to 50 fps, poll input/audio */
void  gfx_set_view(int view_mode, int y0);
/* 256 lines on 240: by default each band of 16 lines leaves out one that
 * repeats the line above (so text keeps all its rows); 0 = plain 15/16
 * scaling, for games that place underlay cels with gfx_display_y() */
void  gfx_set_line_drop(int smart);
/* Hardware layers under the frame (GFX_PAL32): pixels in the transparent
 * pen show what the underlay cels drew. gfx_set_underlay() takes a CCB
 * list (last one flagged CCB_LAST), drawn before the frame at every
 * gfx_swap() in display coordinates (320x240); gfx_display_y() maps a
 * logical y (16.16) to display y for them. See projects/ballblazer. */
void  gfx_set_transparent_pen(int pen);   /* -1: none (default) */
void  gfx_set_underlay(void *ccb_list);   /* NULL: none */
LONG  gfx_display_y(LONG y16);
/* an 8-bit pen buffer as an underlay cel in the screen palette (a
 * scrolling background drawn once), and where to put it (game pixels) */
void *gfx_layer_cel(UBYTE *pens, int w, int h, int stride);
void  gfx_layer_move(void *cel, LONG x, LONG y);
void *gfx_layer_chain(void *a, void *b);  /* a then b as one list; returns a */
/* draw into another pen buffer (to fill a layer); returns the previous
 * target, NULL = the frame */
UBYTE *gfx_draw_to(UBYTE *buf);
/* the same for GFX_RGB16 (colour buffers; stride in pixels) */
void *gfx_layer_cel16(UWORD *pix, int w, int h, int stride);
UWORD *gfx_draw_to16(UWORD *buf);
void  gfx_set_rate(int hz);            /* 50 (default) or 60 */
void  gfx_set_rgb4(int pen, UWORD rgb4);               /* $0RGB */
void  gfx_set_rgb24(int pen, ULONG rgb24);              /* $RRGGBB */
void  gfx_load_rgb4(const UWORD *rgb4, int n);
ULONG gfx_frame(void);                 /* frames shown since gfx_init */
/* For games too heavy to draw at 50 fps: call once per loop and run the
 * game logic this many times (1..4) before drawing, so the game keeps its
 * real speed while the screen updates less often. Logs
 * "AMIGA3DO: steps=N" every 250 steps (tests measure game speed from it). */
int   gfx_steps(void);
/* catch-up limit for gfx_steps() (default 4): raise it for games that draw
 * below 12 fps so their logic still keeps time */
void  gfx_set_max_steps(int n);
int   gfx_height(void);
/* 8x8-font text scaled by `scale`, top-left at x,y, in the RastPort's APen */
void  gfx_text_big(struct RastPort *rp, LONG x, LONG y, const char *s, int scale);
UBYTE *gfx_pixels8(void);              /* GFX_PAL32 buffer, GFX_WIDTH bytes per row */
/* Draw a w x h byte image at x,y (clipped to rp); 0 bytes are transparent.
 * pen < 0: bytes are pens; pen >= 0: every non-zero byte is drawn in pen.
 * The ARM60 has no cache, so pre-render sprites/glyphs that the original
 * draws with many tiny RectFills and blit them with this instead. */
void  gfx_blit8(struct RastPort *rp, LONG x, LONG y, const UBYTE *img, int w, int h, int pen);
/* Faster for small images drawn often (sprites, font glyphs): convert once
 * into horizontal runs, then each draw is ~one store per visible pixel.
 * Same pen rule as gfx_blit8. */
/* Fill ncols columns (column i spans x0+i*colw .. +colw-1) from top[i]
 * down to bottom (inclusive; top[i] > bottom leaves it empty) in pen.
 * Same pixels as one RectFill per column, but the rows every column
 * covers are filled as whole spans: much faster for height-map scenery
 * (mountains, terrain) on the cache-less ARM60. */
void  gfx_fill_columns(struct RastPort *rp, LONG x0, int ncols, int colw,
                       const WORD *top, LONG bottom, int pen);
typedef struct GfxSprite GfxSprite;
GfxSprite *gfx_sprite_make(const UBYTE *img, int w, int h);
void  gfx_sprite_draw(struct RastPort *rp, const GfxSprite *s, LONG x, LONG y, int pen);
void  gfx_sprite_free(GfxSprite *s);
UWORD *gfx_pixels16(void);             /* GFX_RGB16 buffer */
UWORD gfx_pen_rgb16(int pen);         /* palette pen -> 16-bit pixel value */

/* ---- input ---- */
#define PAD_UP     0x0001
#define PAD_DOWN   0x0002
#define PAD_LEFT   0x0004
#define PAD_RIGHT  0x0008
#define PAD_A      0x0010
#define PAD_B      0x0020
#define PAD_C      0x0040
#define PAD_P      0x0080   /* play/pause (start) */
#define PAD_X      0x0100   /* stop: quits to the menu by convention */
#define PAD_L      0x0200
#define PAD_R      0x0400

ULONG pad_held(int port);              /* port 0 or 1: buttons currently down */
ULONG pad_pressed(int port);           /* buttons that went down this frame */
int   amiga_quit_requested(void);      /* X pressed on pad 0 */

/* ---- misc ---- */
void  amiga_set_progdir(const char *dir);   /* where "PROGDIR:" / relative files live on disc */
void  amiga_log(const char *fmt, ...);      /* tdo_log with the game's prefix */

#ifdef __cplusplus
}
#endif

#endif
