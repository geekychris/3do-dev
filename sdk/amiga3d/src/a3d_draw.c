// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_draw.c - the shared engine's drawing (see a3d_draw.h). The span
 * filler and the bitplane writes come from Terra Strike and ScAIry Fun;
 * text is drawn by the CPU from the topaz 8 bitmap on both Amigas, so the
 * only blitter work is g_fill.
 */
#ifdef AMIGA3DO
#include <string.h>
#include <stdlib.h>
#include "graphics.h"                   /* the 3DO's CCB, CreateCel */
#include "celutils.h"
#include "a3d.h"
#include "a3d_draw.h"
#include "a3d_i64.h"
#include "font8x8_basic.h"              /* sdk/amiga/src: the 3DO has no topaz 8 */

/* ---------- the font ---------- */
static uint8_t glyph[256][8];           /* one byte per row, bit 7 left */
static int glyph_base = 6;

int a3_draw_init(void)
{
    int c, r, b;
    for (c = 0; c < 128; c++)
        for (r = 0; r < 8; r++) {
            uint8_t bits = 0, src = font8x8_basic[c][r];
            for (b = 0; b < 8; b++) if (src & (1 << b)) bits |= (uint8_t)(0x80 >> b);   /* bit 0 is its left */
            glyph[c][r] = bits;
        }
    return 1;
}
#else
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <string.h>

#include "a3d.h"
#include "a3d_draw.h"

/* ---------- the font ---------- */
static uint8_t glyph[256][8];           /* topaz 8, one byte per row, bit 7 left */
static int glyph_base = 6;

int a3_draw_init(void)
{
    struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, 0 };
    struct TextFont *tf = OpenFont(&ta);
    int c, r, b;
    if (!tf) return 1;                  /* no text, but carry on */
    glyph_base = tf->tf_Baseline;
    for (c = tf->tf_LoChar; c <= tf->tf_HiChar && c < 256; c++) {
        const ULONG *loc = (const ULONG *)tf->tf_CharLoc + (c - tf->tf_LoChar);
        int off = (int)(*loc >> 16), wd = (int)(*loc & 0xFFFF);
        const UBYTE *data = (const UBYTE *)tf->tf_CharData;
        if (wd > 8) wd = 8;
        for (r = 0; r < 8 && r < tf->tf_YSize; r++) {
            uint8_t bits = 0;
            for (b = 0; b < wd; b++) {
                int bit = off + b;
                if (data[r * tf->tf_Modulo + (bit >> 3)] & (0x80 >> (bit & 7))) bits |= 0x80 >> b;
            }
            glyph[c][r] = bits;
        }
    }
    CloseFont(tf);
    return 1;
}

#endif

int g_clip_y0 = 0, g_clip_y1 = SCREEN_H - 1;

#ifdef __PPC__
/* ---------- primitives: full colour at twice the resolution ---------- */
uint32_t a3_lut[32];
int a3_sky_pen = -1, a3_sky_rows;
uint32_t a3_sky_col[PHYS_H];
int a3_order;

/* an A R G B colour in the canvas's format; a 16-bit one twice over (two
 * pixels to a 32-bit store) */
static uint32_t native(uint32_t c)
{
    uint32_t v;
    switch (a3_order) {
    case 1: return (c & 0xFF) << 24 | (c & 0xFF00) << 8 | (c >> 8 & 0xFF00) | c >> 24;
    case 2: return c << 8 | c >> 24;
    case 3: case 4:
        v = ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
        if (a3_order == 4) v = ((v & 0xFF) << 8) | (v >> 8);
        return v | v << 16;
    }
    return c;
}

/* n 32-bit words of v from d (8-byte aligned): two at a time through
 * 8-byte floating-point stores (the 440 has no 64-bit integer store, and
 * GCC would split a double store into two integer ones), unrolled: every
 * store costs a lot under QEMU's emulation */
static uint32_t *fill_words(uint32_t *d, uint32_t v, int n)
{
    static uint32_t two[2] __attribute__((aligned(8)));
    double f;
    int pairs = n >> 1;
    two[0] = two[1] = v;
    __asm__ volatile ("lfd %0,0(%1)" : "=f"(f) : "b"(two) : "memory");
    while (pairs >= 8) {
        __asm__ volatile ("stfd %0,0(%1)\n\tstfd %0,8(%1)\n\tstfd %0,16(%1)\n\tstfd %0,24(%1)\n\t"
                          "stfd %0,32(%1)\n\tstfd %0,40(%1)\n\tstfd %0,48(%1)\n\tstfd %0,56(%1)"
                          : : "f"(f), "b"(d) : "memory");
        d += 16; pairs -= 8;
    }
    while (pairs-- > 0) { __asm__ volatile ("stfd %0,0(%1)" : : "f"(f), "b"(d) : "memory"); d += 2; }
    if (n & 1) *d++ = v;
    return d;
}

/* a span in the drawing's own pixels, no clipping, a native colour */
static void pspan_c(Canvas *cv, int py, int xl, int xr, uint32_t c)
{
    int n = xr - xl + 1;
    if (n <= 0) return;
    if (cv->bpp == 4) {
        uint32_t *d = (uint32_t *)cv->pix + py * cv->w + xl;
        if ((uintptr_t)d & 4) { *d++ = c; n--; }
        fill_words(d, c, n);
    } else {
        uint16_t *d = (uint16_t *)cv->pix + py * cv->w + xl;
        while (((uintptr_t)d & 7) && n > 0) { *d++ = (uint16_t)c; n--; }
        d = (uint16_t *)fill_words((uint32_t *)d, c, n >> 1);
        if (n & 1) *d = (uint16_t)c;
    }
}

static void pspan(Canvas *cv, int py, int xl, int xr, int col)
{
    pspan_c(cv, py, xl, xr, native((col == a3_sky_pen && py < a3_sky_rows) ? a3_sky_col[py] : a3_lut[col & 31]));
}

void g_fill_grad(Canvas *cv, int y0, int y1, uint32_t top, uint32_t bottom)
{
    int py, p0 = y0 * A3_SUB, p1 = y1 * A3_SUB + A3_SUB - 1, n;
    if (p0 < g_clip_y0 * A3_SUB) p0 = g_clip_y0 * A3_SUB;
    if (p1 > g_clip_y1 * A3_SUB + A3_SUB - 1) p1 = g_clip_y1 * A3_SUB + A3_SUB - 1;
    n = y1 * A3_SUB + A3_SUB - 1 - y0 * A3_SUB;
    for (py = p0; py <= p1; py++) {
        int t = n > 0 ? ((py - y0 * A3_SUB) * 256) / n : 0, c;
        uint32_t out = 0;
        for (c = 16; c >= 0; c -= 8) {
            int a = (int)(top >> c & 255), b = (int)(bottom >> c & 255);
            out = (out << 8) | (uint32_t)(a + ((b - a) * t) / 256);
        }
        pspan_c(cv, py, 0, PHYS_W - 1, native(0xFF000000UL | out));
    }
}

void g_span(Canvas *cv, int y, int xl, int xr, int col)
{
    pspan(cv, y * 2, xl * 2, xr * 2 + 1, col);
    pspan(cv, y * 2 + 1, xl * 2, xr * 2 + 1, col);
}

void g_fill(Canvas *cv, int x0, int y0, int x1, int y1, int col)
{
    g_rect(cv, x0, y0, x1, y1, col);
}

void g_sync(void) {}

#include "a3d_font16.h"

/* a character from the 16 x 16 font (tools/make_font.py), each of its
 * pixels sc x sc of the drawing's; x, y: its cell's top left in the games'
 * coordinates. 0 if it isn't in the font. */
static int glyph16(Canvas *cv, int x, int y, int col, int c, int sc)
{
    int r, b, py0 = g_clip_y0 * 2, py1 = g_clip_y1 * 2 + 1, k;
    const uint16_t *g;
    if (c < 32 || c > 126) return 0;
    g = a3_font16[c - 32];
    for (r = 0; r < 16; r++) {
        uint16_t bits = g[r];
        if (!bits) continue;
        for (b = 0; b < 16; b++)
            if (bits & (0x8000 >> b)) {
                int e = b, xl, xr;
                while (e < 15 && (bits & (0x8000 >> (e + 1)))) e++;
                xl = x * 2 + b * sc; xr = x * 2 + e * sc + sc - 1;
                if (xl < 0) xl = 0;
                if (xr > PHYS_W - 1) xr = PHYS_W - 1;
                for (k = 0; k < sc; k++) {
                    int py = y * 2 + r * sc + k;
                    if (py >= py0 && py <= py1 && xl <= xr) pspan(cv, py, xl, xr, col);
                }
                b = e;
            }
    }
    return 1;
}

static void g_glyph(Canvas *cv, int x, int y, int col, int c)
{
    int r, b;
    if (x < 0 || x > SCREEN_W - 8) return;
    if (glyph16(cv, x, y, col, c & 255, 1)) return;
    for (r = 0; r < 8; r++) {
        uint8_t bits = glyph[c & 255][r];
        int yy = y + r;
        if (!bits || yy < g_clip_y0 || yy > g_clip_y1) continue;
        for (b = 0; b < 8; b++)
            if (bits & (0x80 >> b)) {
                int e = b;
                while (e < 7 && (bits & (0x80 >> (e + 1)))) e++;
                g_span(cv, yy, x + b, x + e, col);
                b = e;
            }
    }
}

/* ---------- the tall picture: full colour, twice the resolution ---------- */
static Canvas pic;
static int pic_h;                       /* in the games' rows */
static int pic_order;

Canvas *g_pic_alloc(int h)
{
    g_pic_free();
    pic.pix = AllocVecTags((uint32)(PHYS_W * h * A3_SUB * 4), AVT_Type, MEMF_PRIVATE, AVT_ClearWithValue, 0, TAG_DONE);
    if (!pic.pix) return NULL;
    pic.w = PHYS_W; pic.h = h * A3_SUB;
    pic.bpp = A3_BPP(a3_order);         /* drawn in the canvas's format */
    pic_order = a3_order;
    pic_h = h;
    return &pic;
}

/* a pixel from one format to another (when the display changed under a picture) */
static uint32_t to_argb(uint32_t v, int order)
{
    switch (order) {
    case 1: return (v & 0xFF) << 24 | (v & 0xFF00) << 8 | (v >> 8 & 0xFF00) | v >> 24;
    case 2: return v >> 8 | v << 24;
    case 4: v = ((v & 0xFF) << 8) | ((v >> 8) & 0xFF);       /* fall through */
    case 3: return 0xFF000000UL | (v & 0xF800) << 8 | (v & 0x07E0) << 5 | (v & 0x001F) << 3;
    }
    return v;
}

void g_pic_blit(Canvas *cv, int src_y, int dst_y, int rows)
{
    int r, x;
    if (!pic.pix || src_y < 0 || src_y + rows > pic_h || rows <= 0) return;
    for (r = 0; r < rows * A3_SUB; r++) {
        uint8_t *d = (uint8_t *)cv->pix + ((dst_y * A3_SUB + r) * cv->w) * cv->bpp;
        const uint8_t *s = (const uint8_t *)pic.pix + ((src_y * A3_SUB + r) * pic.w) * pic.bpp;
        if (pic_order == a3_order) {
            memcpy(d, s, PHYS_W * cv->bpp);     /* a row at a time: the guest's memcpy can leave part of a big copy undone */
            continue;
        }
        for (x = 0; x < PHYS_W; x++) {          /* the display's format changed: convert */
            uint32_t v = pic.bpp == 4 ? ((const uint32_t *)s)[x] : ((const uint16_t *)s)[x];
            uint32_t n = native(to_argb(v, pic_order));
            if (cv->bpp == 4) ((uint32_t *)d)[x] = n; else ((uint16_t *)d)[x] = (uint16_t)n;
        }
    }
}

void g_pic_free(void)
{
    if (pic.pix) FreeVec(pic.pix);
    pic.pix = NULL;
    pic_h = 0;
}

/* ---------- pictures ---------- */
struct A3Image {
    int w, h;                           /* in the drawing's pixels */
    uint8_t *rgb;
    void *cache;                        /* in the canvas's format */
    int cache_order;
};

A3Image *g_image_load(const char *path)
{
    BPTR f = Open((CONST_STRPTR)path, MODE_OLDFILE);
    uint8_t hdr[8];
    A3Image *im = NULL;
    if (!f) return NULL;
    if (Read(f, hdr, 8) == 8 && !memcmp(hdr, "A3IM", 4)) {
        int w = hdr[4] << 8 | hdr[5], h = hdr[6] << 8 | hdr[7];
        int32 n = w * h * 3;
        im = (A3Image *)AllocVecTags(sizeof(A3Image), AVT_ClearWithValue, 0, TAG_DONE);
        if (im) {
            im->w = w; im->h = h; im->cache_order = -1;
            im->rgb = (uint8_t *)AllocVecTags((uint32)n, AVT_Type, MEMF_PRIVATE, TAG_DONE);
            if (!im->rgb || Read(f, im->rgb, n) != n) { g_image_free(im); im = NULL; }
        }
    }
    Close(f);
    return im;
}

void g_image_free(A3Image *im)
{
    if (!im) return;
    if (im->rgb) FreeVec(im->rgb);
    if (im->cache) FreeVec(im->cache);
    FreeVec(im);
}

static int image_cache(Canvas *cv, A3Image *im)
{
    int i, n = im->w * im->h, bpp = cv->bpp;
    if (im->cache_order == a3_order) return 1;
    if (im->cache) FreeVec(im->cache);
    im->cache = AllocVecTags((uint32)(n * 4), AVT_Type, MEMF_PRIVATE, TAG_DONE);
    if (!im->cache) return 0;
    for (i = 0; i < n; i++) {
        const uint8_t *p = im->rgb + i * 3;
        uint32_t v = native(0xFF000000UL | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]);
        if (bpp == 4) ((uint32_t *)im->cache)[i] = v; else ((uint16_t *)im->cache)[i] = (uint16_t)v;
    }
    im->cache_order = a3_order;
    return 1;
}

int g_image_w(const A3Image *im) { return im ? im->w : 0; }
int g_image_h(const A3Image *im) { return im ? im->h : 0; }

void g_image_row(Canvas *cv, A3Image *im, int sy, int x0, int x1, int dx, int dy)
{
    int bpp = cv->bpp;
    if (!im || sy < 0 || sy >= im->h || !image_cache(cv, im)) return;
    if (dy < g_clip_y0 * A3_SUB || dy > g_clip_y1 * A3_SUB + A3_SUB - 1 || dy >= cv->h) return;
    if (x0 < 0) x0 = 0;
    if (x1 > im->w - 1) x1 = im->w - 1;
    if (dx + x0 < 0) x0 = -dx;
    if (dx + x1 > PHYS_W - 1) x1 = PHYS_W - 1 - dx;
    if (x0 > x1) return;
    memcpy((uint8_t *)cv->pix + (dy * cv->w + dx + x0) * bpp, (uint8_t *)im->cache + (sy * im->w + x0) * bpp, (size_t)((x1 - x0 + 1) * bpp));
}

void g_image_wrap(Canvas *cv, A3Image *im, int sx, int top, int y0, int y1)
{
    int bpp = cv->bpp, y;
    if (!im || !image_cache(cv, im)) return;
    if (y0 < g_clip_y0 * A3_SUB) y0 = g_clip_y0 * A3_SUB;
    if (y1 > g_clip_y1 * A3_SUB + A3_SUB - 1) y1 = g_clip_y1 * A3_SUB + A3_SUB - 1;
    if (y1 > cv->h - 1) y1 = cv->h - 1;
    sx %= im->w;
    if (sx < 0) sx += im->w;
    for (y = y0; y <= y1; y++) {
        int sy = y - top, x = 0, c = sx;
        uint8_t *src, *dst = (uint8_t *)cv->pix + y * cv->w * bpp;
        if (sy < 0) sy = 0;
        if (sy > im->h - 1) sy = im->h - 1;
        src = (uint8_t *)im->cache + sy * im->w * bpp;
        while (x < PHYS_W) {
            int n = im->w - c;
            if (n > PHYS_W - x) n = PHYS_W - x;
            memcpy(dst + x * bpp, src + c * bpp, (size_t)(n * bpp));
            x += n;
            c = 0;
        }
    }
}

void g_image_draw(Canvas *cv, A3Image *im, int x, int y)
{
    int bpp = cv->bpp, r, px0 = x * A3_SUB, py0 = y * A3_SUB, xs = 0, w;
    int ylo = g_clip_y0 * A3_SUB, yhi = g_clip_y1 * A3_SUB + A3_SUB - 1;
    if (!im || !image_cache(cv, im)) return;     /* converted once for the display's format */
    if (px0 < 0) { xs = -px0; px0 = 0; }
    w = im->w - xs;
    if (px0 + w > PHYS_W) w = PHYS_W - px0;
    if (w <= 0) return;
    for (r = 0; r < im->h; r++) {
        int py = py0 + r;
        if (py < ylo || py > yhi || py >= cv->h) continue;
        memcpy((uint8_t *)cv->pix + (py * cv->w + px0) * bpp, (uint8_t *)im->cache + (r * im->w + xs) * bpp, (size_t)(w * bpp));
    }
}

#elif defined(AMIGA3DO)
/* ---------- primitives: 8-bit pens (the 3DO, the frame of sdk/amiga's layer) ---------- */
extern void amiga_fill64(void *dst, uint32_t pattern, uint32_t blocks);   /* sdk/amiga/src/fill.s: STM */

/* n bytes of pen from p: word stores for short runs, STM blocks for long */
static void fill8(uint8_t *p, int pen, int32_t n)
{
    uint32_t pw = (uint32_t)(pen & 255) * 0x01010101UL, *q, blocks;
    if (n < 8) { while (n-- > 0) *p++ = (uint8_t)pen; return; }
    while (((uint32_t)p & 3) && n) { *p++ = (uint8_t)pen; n--; }
    q = (uint32_t *)p;
    if (n >= 96) {
        blocks = (uint32_t)n >> 6;
        amiga_fill64(q, pw, blocks);
        q += blocks << 4;
        n &= 63;
    }
    while (n >= 16) { q[0] = pw; q[1] = pw; q[2] = pw; q[3] = pw; q += 4; n -= 16; }
    while (n >= 4) { *q++ = pw; n -= 4; }
    p = (uint8_t *)q;
    while (n-- > 0) *p++ = (uint8_t)pen;
}

/* n 16-bit pixels of c from p: two to a word store (no halfword stores) */
static void fill16(uint16_t *p, uint16_t c, int32_t n)
{
    uint32_t cc = ((uint32_t)c << 16) | c, *q, blocks;
    if (n <= 0) return;
    if ((uint32_t)p & 2) { *p++ = c; n--; }
    q = (uint32_t *)p;
    if (n >= 64) {
        blocks = (uint32_t)n >> 5;
        amiga_fill64(q, cc, blocks);
        q += blocks << 4;
        n &= 31;
    }
    while (n >= 8) { q[0] = cc; q[1] = cc; q[2] = cc; q[3] = cc; q += 4; n -= 8; }
    while (n >= 2) { *q++ = cc; n -= 2; }
    if (n) *(uint16_t *)q = c;
}

/* ---------- the overlays: what is drawn after the 3D, in 16-bit colour ---------- */
#define A3_OVL 2
static uint16_t *ovl_pix[A3_OVL];
static CCB *ovl_cel[A3_OVL];
static uint32_t ovl_bands[A3_OVL];      /* 16-row bands drawn since the last clear */
static int ovl_cur = -1;                /* the one being drawn into, -1: none */
static uint16_t pen15[32];              /* the pens' colours, never 0 (transparent) */
static int sky_pen = -1, sky_stride;
static const uint16_t *sky_rows;

void a3_set_sky(int pen, const uint16_t *rows15, int stride)
{
    sky_pen = pen;
    sky_rows = rows15;
    sky_stride = stride;
}

extern unsigned short gfx_pen_rgb16(int pen);
static void refresh_pens(void)
{
    int i;
    for (i = 0; i < 32; i++) {
        uint16_t c = gfx_pen_rgb16(i);
        pen15[i] = c ? c : 1;
    }
}

static uint16_t col16(int col, int y)
{
    if (col == sky_pen && sky_rows && y >= 0 && y < SCREEN_H) return sky_rows[y * sky_stride];
    return pen15[col & 31];
}

#define dirty(y) (ovl_bands[ovl_cur] |= 1UL << ((y) >> 4))

void g_span(Canvas *cv, int y, int xl, int xr, int col)
{
    if (cv->bpp == 2) {
        dirty(y);
        fill16((uint16_t *)cv->pix + y * cv->w + xl, col16(col, y), xr - xl + 1);
    } else fill8(cv->pix + y * cv->w + xl, col, xr - xl + 1);
}

void g_fill(Canvas *cv, int x0, int y0, int x1, int y1, int col)
{
    g_rect(cv, x0, y0, x1, y1, col);
}

void g_sync(void) {}

#define pspan g_span                    /* the drawing's pixels are the games' */

/* ---------- the tall picture: a pen buffer ---------- */
static Canvas pic;
static int pic_h;

Canvas *g_pic_alloc(int h)
{
    g_pic_free();
    pic.pix = (uint8_t *)malloc((size_t)(SCREEN_W * h));
    if (!pic.pix) return NULL;
    memset(pic.pix, 0, (size_t)(SCREEN_W * h));
    pic.w = SCREEN_W;
    pic.h = h;
    pic.bpp = 1;
    pic_h = h;
    return &pic;
}

void g_pic_blit(Canvas *cv, int src_y, int dst_y, int rows)
{
    if (!pic.pix || src_y < 0 || src_y + rows > pic_h || rows <= 0) return;
    if (cv->bpp == 2) {                 /* over the 3D: pen by pen */
        int r, x;
        for (r = 0; r < rows; r++) {
            const uint8_t *s = pic.pix + (src_y + r) * SCREEN_W;
            uint16_t *d = (uint16_t *)cv->pix + (dst_y + r) * cv->w;
            dirty(dst_y + r);
            for (x = 0; x < SCREEN_W; x++) d[x] = col16(s[x], dst_y + r);
        }
        return;
    }
    memcpy(cv->pix + dst_y * cv->w, pic.pix + src_y * SCREEN_W, (size_t)(SCREEN_W * rows));
}

void g_pic_free(void)
{
    if (pic.pix) free(pic.pix);
    pic.pix = NULL;
    pic_h = 0;
}

/* full-colour pictures: none (the games keep their 32-colour look, as on the classic Amiga) */
A3Image *g_image_load(const char *path) { (void)path; return NULL; }
void g_image_draw(Canvas *cv, A3Image *im, int x, int y) { (void)cv; (void)im; (void)x; (void)y; }
void g_image_free(A3Image *im) { (void)im; }
void g_image_row(Canvas *cv, A3Image *im, int sy, int x0, int x1, int dx, int dy) { (void)cv; (void)im; (void)sy; (void)x0; (void)x1; (void)dx; (void)dy; }
void g_fill_grad(Canvas *cv, int y0, int y1, uint32_t top, uint32_t bottom) { (void)cv; (void)y0; (void)y1; (void)top; (void)bottom; }
void g_image_wrap(Canvas *cv, A3Image *im, int sx, int top, int y0, int y1) { (void)cv; (void)im; (void)sx; (void)top; (void)y0; (void)y1; }
int g_image_w(const A3Image *im) { (void)im; return 0; }
int g_image_h(const A3Image *im) { (void)im; return 0; }

static void g_glyph(Canvas *cv, int x, int y, int col, int c)
{
    int r, b;
    uint8_t pen = (uint8_t)col;
    if (x < 0 || x > SCREEN_W - 8) return;
    for (r = 0; r < 8; r++) {
        uint8_t bits = glyph[c & 255][r], *d;
        int yy = y + r;
        if (!bits || yy < g_clip_y0 || yy > g_clip_y1) continue;
        if (cv->bpp == 2) {
            uint16_t *d16 = (uint16_t *)cv->pix + yy * cv->w + x, c = col16(col, yy);
            dirty(yy);
            for (b = 0; b < 8; b++) if (bits & (0x80 >> b)) d16[b] = c;
            continue;
        }
        d = cv->pix + yy * cv->w + x;
        for (b = 0; b < 8; b++) if (bits & (0x80 >> b)) d[b] = pen;
    }
}

#else
/* ---------- primitives: five bitplanes ---------- */
/* Works for interleaved and plain bitmaps (BytesPerRow covers both). */
void g_span(Canvas *rp, int y, int xl, int xr, int col)
{
    PLANEPTR *planes = rp->BitMap->Planes;
    LONG bpr = rp->BitMap->BytesPerRow;
    int wl = xl >> 4, wr = xr >> 4, p, k, n = wr - wl;
    UWORD lm = 0xFFFF >> (xl & 15), rm = 0xFFFF << (15 - (xr & 15));
    LONG off = y * bpr + wl * 2;

    if (n == 0) lm &= rm;
    for (p = 0; p < 5; p++, col >>= 1) {
        UWORD *d = (UWORD *)(planes[p] + off);
        if (col & 1) {
            d[0] |= lm;
            if (n) { for (k = 1; k < n; k++) d[k] = 0xFFFF; d[n] |= rm; }
        } else {
            d[0] &= ~lm;
            if (n) { for (k = 1; k < n; k++) d[k] = 0; d[n] &= ~rm; }
        }
    }
}

void g_fill(Canvas *rp, int x0, int y0, int x1, int y1, int col)
{
    if (x0 < 0) x0 = 0;
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    if (x0 > x1 || y0 > y1) return;
    SetDrMd(rp, JAM1);
    SetAPen(rp, col);
    RectFill(rp, x0, y0, x1, y1);
}

void g_sync(void) { WaitBlit(); }

#define pspan g_span                    /* the drawing's pixels are the games' */

/* ---------- the tall picture: a five-plane bitmap in chip memory ---------- */
static struct RastPort pic_rp;
static struct BitMap *pic_bm;
static int pic_h;

Canvas *g_pic_alloc(int h)
{
    g_pic_free();
    pic_bm = AllocBitMap(SCREEN_W, (ULONG)h, 5, BMF_CLEAR, NULL);   /* no friend: plain planes, chip memory */
    if (!pic_bm) return NULL;
    InitRastPort(&pic_rp);
    pic_rp.BitMap = pic_bm;
    pic_h = h;
    return &pic_rp;
}

void g_pic_blit(Canvas *rp, int src_y, int dst_y, int rows)
{
    if (!pic_bm || src_y < 0 || src_y + rows > pic_h || rows <= 0) return;
    BltBitMap(pic_bm, 0, src_y, rp->BitMap, 0, dst_y, SCREEN_W, rows, 0xC0, 0x1F, NULL);
    WaitBlit();                         /* the CPU draws over it next */
}

void g_pic_free(void)
{
    if (pic_bm) { WaitBlit(); FreeBitMap(pic_bm); }
    pic_bm = NULL;
    pic_h = 0;
}

/* pictures need full colour: AmigaOS 4 only */
A3Image *g_image_load(const char *path) { (void)path; return NULL; }
void g_image_draw(Canvas *cv, A3Image *im, int x, int y) { (void)cv; (void)im; (void)x; (void)y; }
void g_image_free(A3Image *im) { (void)im; }
void g_image_row(Canvas *cv, A3Image *im, int sy, int x0, int x1, int dx, int dy) { (void)cv; (void)im; (void)sy; (void)x0; (void)x1; (void)dx; (void)dy; }
void g_fill_grad(Canvas *cv, int y0, int y1, uint32_t top, uint32_t bottom) { (void)cv; (void)y0; (void)y1; (void)top; (void)bottom; }
void g_image_wrap(Canvas *cv, A3Image *im, int sx, int top, int y0, int y1) { (void)cv; (void)im; (void)sx; (void)top; (void)y0; (void)y1; }
int g_image_w(const A3Image *im) { (void)im; return 0; }
int g_image_h(const A3Image *im) { (void)im; return 0; }

static void g_glyph(Canvas *rp, int x, int y, int col, int c)
{
    PLANEPTR *planes = rp->BitMap->Planes;
    LONG bpr = rp->BitMap->BytesPerRow;
    int r, p, wl = x >> 4, sh = x & 15;
    if (x < 0 || x > SCREEN_W - 8) return;
    for (r = 0; r < 8; r++) {
        int yy = y + r, cc = col;
        ULONG m = (ULONG)glyph[c & 255][r] << (24 - sh);
        UWORD hi = (UWORD)(m >> 16), lo = (UWORD)m;
        LONG off;
        if (!m || yy < g_clip_y0 || yy > g_clip_y1) continue;
        off = yy * bpr + wl * 2;
        for (p = 0; p < 5; p++, cc >>= 1) {
            UWORD *d = (UWORD *)(planes[p] + off);
            if (cc & 1) { d[0] |= hi; if (lo) d[1] |= lo; }
            else        { d[0] &= ~hi; if (lo) d[1] &= ~lo; }
        }
    }
}
#endif

/* ---------- 2D, both ---------- */
#ifdef AMIGA3DO
#define MULDIV(a, b, c) a3_muldiv((a), (b), (c))
#define DOT3_NONNEG(a, b, c, d, e, f) a3_dot3_nonneg((a), (b), (c), (d), (e), (f))
#else
#define MULDIV(a, b, c) ((int32_t)(((int64_t)(a) * (b)) / (c)))
#define DOT3_NONNEG(a, b, c, d, e, f) ((int64_t)(a) * (b) + (int64_t)(c) * (d) + (int64_t)(e) * (f) >= 0)
#endif

const uint8_t *g_glyph_bits(int c) { return glyph[c & 255]; }

void g_rect(Canvas *cv, int x0, int y0, int x1, int y1, int col)
{
    int y;
    if (x0 < 0) x0 = 0;
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    if (x0 > x1) return;
    for (y = y0; y <= y1; y++) g_span(cv, y, x0, x1, col);
}

void g_hline(Canvas *cv, int x0, int x1, int y, int col)
{
    if (y < g_clip_y0 || y > g_clip_y1) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (x0 <= x1) g_span(cv, y, x0, x1, col);
}

void g_vline(Canvas *cv, int x, int y0, int y1, int col)
{
    int y;
    if (x < 0 || x >= SCREEN_W) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    for (y = y0; y <= y1; y++) g_span(cv, y, x, x, col);
}

#ifdef AMIGA3DO
/* The 3DO's rasteriser: the same spans as poly_spans (both end rows of
 * each edge, a shared edge stepped the same way in both polygons, so no
 * gaps), but in 32-bit arrays (the ARM60 has no halfword loads or stores),
 * the rows clipped once instead of tested each step, a reciprocal table
 * instead of a divide per edge (no hardware divide), and each row filled
 * here. */
static int32_t span_l[SPAN_MAX], span_r[SPAN_MAX];
static int32_t recip[SPAN_MAX + 1];     /* 2^20 / dy */

static void raster(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col, int py0, int py1, int widen)
{
    int i, y, ymin = ys[0], ymax = ys[0], r0, r1, w = PHYS_W - 1;
    uint8_t *row;
    if (!recip[1]) for (i = 1; i <= SPAN_MAX; i++) recip[i] = (int32_t)((1L << 20) / i);
    for (i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    if (ymax - ymin >= SPAN_MAX) ymax = ymin + SPAN_MAX - 1;
    r0 = ymin < py0 ? py0 : ymin;       /* the rows drawn */
    r1 = ymax + widen > py1 ? py1 : ymax + widen;
    if (r0 > r1) return;
    for (y = ymin; y <= ymax; y++) { span_l[y - ymin] = 32767; span_r[y - ymin] = -32768; }
    for (i = 0; i < n; i++) {
        int j = i + 1 < n ? i + 1 : 0;
        int x0 = xs[i], y0 = ys[i], x1 = xs[j], y1 = ys[j], ya, yb;
        int32_t x, dx, *l, *rr;
        if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        if (y1 > ymax) y1 = ymax;
        if (y1 <= y0) dx = 0;
        else if (x1 - x0 < 2048 && x0 - x1 < 2048) dx = ((x1 - x0) * recip[y1 - y0]) >> 4;
        else dx = (((int32_t)(x1 - x0)) << 16) / (y1 - y0);   /* far off the screen */
        x = ((int32_t)x0 << 16) + 0x8000;
        ya = y0; yb = y1 - 1;           /* the last row takes x1 exactly */
        l = span_l + (ya - ymin);
        rr = span_r + (ya - ymin);
        for (y = ya; y <= yb; y++, x += dx, l++, rr++) {
            int32_t px = x >> 16;
            if (px < *l) *l = px;
            if (px > *rr) *rr = px;
        }
        if (x1 < span_l[y1 - ymin]) span_l[y1 - ymin] = x1;
        if (x1 > span_r[y1 - ymin]) span_r[y1 - ymin] = x1;
    }
    if (cv->bpp == 2) {
        for (y = r0; y <= r1; y++) {
            int k = y <= ymax ? y - ymin : ymax - ymin;
            int32_t xl = span_l[k], xr = span_r[k] + widen;
            if (xl < 0) xl = 0;
            if (xr > w) xr = w;
            if (xl <= xr) g_span(cv, y, xl, xr, col);
        }
        return;
    }
    row = cv->pix + r0 * cv->w;
    for (y = r0; y <= r1; y++, row += cv->w) {
        int k = y <= ymax ? y - ymin : ymax - ymin;
        int32_t xl = span_l[k], xr = span_r[k] + widen;
        if (xl < 0) xl = 0;
        if (xr > w) xr = w;
        if (xl <= xr) fill8(row + xl, col, xr - xl + 1);
    }
}
#else
static int16_t span_l[SPAN_MAX], span_r[SPAN_MAX];

/* fill a polygon in the drawing's pixels between rows py0..py1; widen
 * (0 or 1): stretch each span and the last row to cover whole pixels of
 * the games' coordinates, as the classic Amiga does */
static void raster(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col, int py0, int py1, int widen)
{
    int ymin, i, rows = poly_spans(xs, ys, n, span_l, span_r, &ymin), w = PHYS_W - 1;
#ifdef __PPC__
    /* the colour once, and each row filled here: a call a row costs too
     * much under emulation */
    uint32_t c = native(a3_lut[col & 31]);
    int sky = col == a3_sky_pen;
    for (i = 0; i < rows + widen; i++) {
        int k = i < rows ? i : rows - 1, xl = span_l[k], xr = span_r[k] + widen, y = ymin + i, n2;
        if (xl > xr || y < py0 || y > py1) continue;
        if (xl < 0) xl = 0;
        if (xr > w) xr = w;
        n2 = xr - xl + 1;
        if (n2 <= 0) continue;
        if (sky && y < a3_sky_rows) { pspan(cv, y, xl, xr, col); continue; }
        if (cv->bpp == 4) {
            uint32_t *d = (uint32_t *)cv->pix + y * cv->w + xl;
            if (n2 < 4) { while (n2-- > 0) *d++ = c; continue; }
            if ((uintptr_t)d & 4) { *d++ = c; n2--; }
            fill_words(d, c, n2);
        } else {
            uint16_t *d = (uint16_t *)cv->pix + y * cv->w + xl;
            if (n2 < 8) { while (n2-- > 0) *d++ = (uint16_t)c; continue; }
            while (((uintptr_t)d & 7) && n2 > 0) { *d++ = (uint16_t)c; n2--; }
            d = (uint16_t *)fill_words((uint32_t *)d, c, n2 >> 1);
            if (n2 & 1) *d = (uint16_t)c;
        }
    }
#else
    for (i = 0; i < rows + widen; i++) {
        int k = i < rows ? i : rows - 1, xl = span_l[k], xr = span_r[k] + widen, y = ymin + i;
        if (xl > xr || y < py0 || y > py1) continue;
        if (xl < 0) xl = 0;
        if (xr > w) xr = w;
        if (xl <= xr) pspan(cv, y, xl, xr, col);
    }
#endif
}

#endif

void g_poly(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col, int y0, int y1)
{
#if A3_SUB > 1
    int16_t px[MAX_PV * 2 + 8], py[MAX_PV * 2 + 8];
    int i;
    if (n > MAX_PV * 2 + 8) n = MAX_PV * 2 + 8;
    for (i = 0; i < n; i++) { px[i] = (int16_t)(xs[i] * A3_SUB); py[i] = (int16_t)(ys[i] * A3_SUB); }
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    raster(cv, px, py, n, col, y0 * A3_SUB, y1 * A3_SUB + A3_SUB - 1, A3_SUB - 1);
#else
    if (y0 < g_clip_y0) y0 = g_clip_y0;
    if (y1 > g_clip_y1) y1 = g_clip_y1;
    raster(cv, xs, ys, n, col, y0, y1, 0);
#endif
}

void g_span_sub(Canvas *cv, int py, int xl, int xr, int col)
{
    if (py < g_clip_y0 * A3_SUB || py > g_clip_y1 * A3_SUB + A3_SUB - 1) return;
    if (xl < 0) xl = 0;
    if (xr > PHYS_W - 1) xr = PHYS_W - 1;
    if (xl <= xr) pspan(cv, py, xl, xr, col);
}

void g_blend_sub(Canvas *cv, int py, int xl, int xr, int col, int alpha)
{
    int x;
    if (py < g_clip_y0 * A3_SUB || py > g_clip_y1 * A3_SUB + A3_SUB - 1) return;
    if (xl < 0) xl = 0;
    if (xr > PHYS_W - 1) xr = PHYS_W - 1;
    if (xl > xr || alpha <= 0) return;
    if (alpha >= 256) { pspan(cv, py, xl, xr, col); return; }
#ifdef __PPC__
    {
        uint32_t c = (col == a3_sky_pen && py < a3_sky_rows) ? a3_sky_col[py] : a3_lut[col & 31];
        int cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255, k = 256 - alpha;
        for (x = xl; x <= xr; x++) {
            uint32_t v, o;
            if (cv->bpp == 4) v = to_argb(((uint32_t *)cv->pix)[py * cv->w + x], a3_order);
            else v = to_argb(((uint16_t *)cv->pix)[py * cv->w + x], a3_order);
            o = 0xFF000000UL | (uint32_t)((((v >> 16) & 255) * k + cr * alpha) >> 8) << 16 |
                (uint32_t)((((v >> 8) & 255) * k + cg * alpha) >> 8) << 8 | (uint32_t)(((v & 255) * k + cb * alpha) >> 8);
            o = native(o);
            if (cv->bpp == 4) ((uint32_t *)cv->pix)[py * cv->w + x] = o; else ((uint16_t *)cv->pix)[py * cv->w + x] = (uint16_t)o;
        }
    }
#else
    if (alpha < 64) return;
    if (alpha >= 192) { pspan(cv, py, xl, xr, col); return; }
    for (x = xl + ((xl + py) & 1); x <= xr; x += 2) pspan(cv, py, x, x, col);
#endif
}

void g_poly_sub(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col)
{
    raster(cv, xs, ys, n, col, g_clip_y0 * A3_SUB, g_clip_y1 * A3_SUB + A3_SUB - 1, 0);
}

void g_circle(Canvas *cv, int cx, int cy, int r, int col)
{
    int dy, R = r * A3_SUB + (A3_SUB - 1) / 2, PX = cx * A3_SUB + A3_SUB / 2, PY = cy * A3_SUB + A3_SUB / 2;
    int py0 = g_clip_y0 * A3_SUB, py1 = g_clip_y1 * A3_SUB + A3_SUB - 1;
    for (dy = -R; dy <= R; dy++) {
        int w = (int)a3_isqrt((uint32_t)(R * R - dy * dy) + (uint32_t)R / 2), xl = PX - w, xr = PX + w, y = PY + dy;
        if (y < py0 || y > py1) continue;
        if (xl < 0) xl = 0;
        if (xr > PHYS_W - 1) xr = PHYS_W - 1;
        if (xl <= xr) pspan(cv, y, xl, xr, col);
    }
}

void g_text(Canvas *cv, int x, int y, int col, const char *s)
{
    for (; *s; s++, x += 8) g_glyph(cv, x, y - glyph_base, col, (unsigned char)*s);
}

void g_text_shadow(Canvas *cv, int x, int y, int col, const char *s)
{
    g_text(cv, x + 1, y + 1, 0, s);
    g_text(cv, x, y, col, s);
}

void g_text_centre(Canvas *cv, int y, int col, const char *s)
{
    g_text_shadow(cv, (SCREEN_W - (int)strlen(s) * 8) / 2, y, col, s);
}

void g_text_big(Canvas *cv, int x, int y, int col, const char *s, int sc)
{
    int r, b;
#ifdef __PPC__
    {
        /* the 16 x 16 font, each of its pixels sc x sc */
        const char *p;
        int xx = x, all = 1;
        for (p = s; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) all = 0;
        if (all) {
            for (p = s; *p; p++, xx += 8 * sc) glyph16(cv, xx, y, col, (unsigned char)*p, sc);
            return;
        }
    }
#endif
    for (; *s; s++, x += 8 * sc)
        for (r = 0; r < 8; r++) {
            uint8_t bits = glyph[(unsigned char)*s][r];
            for (b = 0; b < 8; b++) {
                int e = b;
                if (!(bits & (0x80 >> b))) continue;
                while (e < 7 && (bits & (0x80 >> (e + 1)))) e++;      /* a run of set pixels */
                g_rect(cv, x + b * sc, y + r * sc, x + e * sc + sc - 1, y + r * sc + sc - 1, col);
                b = e;
            }
        }
}

void g_box(Canvas *cv, int x0, int y0, int x1, int y1, int fill, int edge)
{
    g_rect(cv, x0, y0, x1, y1, fill);
    g_rect(cv, x0, y0, x1, y0, edge);
    g_rect(cv, x0, y1, x1, y1, edge);
    g_rect(cv, x0, y0, x0, y1, edge);
    g_rect(cv, x1, y0, x1, y1, edge);
}

/* ---------- the 3D draw list ---------- */
typedef struct {
    int16_t next;
    uint8_t n, col;
    int16_t x[MAX_PV], y[MAX_PV];
} DPoly;

#if A3_SUB > 1
#define MAX_DPOLY 4000                  /* AmigaOS 4 draws further */
#else
#define MAX_DPOLY 1400
#endif
#define NBUCKET   512
static DPoly pool[MAX_DPOLY];
static int16_t bhead[NBUCKET], btail[NBUCKET];
static int32_t far_z = 4096;
#ifdef AMIGA3DO
static int32_t bucket_k = (NBUCKET << 16) / 4096;   /* (NBUCKET << 16) / far_z: a multiply, not a divide */
#endif
int32_t a3_npolys, a3_dropped;

void a3_begin(int32_t fz)
{
    int i;
    a3_npolys = 0;
    far_z = fz > 0 ? fz : 1;
#ifdef AMIGA3DO
    bucket_k = (int32_t)(((uint32_t)NBUCKET << 16) / (uint32_t)far_z);
#endif
    for (i = 0; i < NBUCKET; i++) bhead[i] = btail[i] = -1;
}

static int bucket_of(int32_t depth)
{
#ifdef AMIGA3DO
    if (depth <= 0) return 0;
    if (depth >= far_z) return NBUCKET - 1;
    return (int)((depth * bucket_k) >> 16);     /* depth * bucket_k < NBUCKET << 16 */
#else
    int32_t b = depth < 0 ? 0 : (depth >= far_z ? NBUCKET - 1 : MULDIV(depth, NBUCKET, far_z));
    if (b < 0) b = 0;
    if (b >= NBUCKET) b = NBUCKET - 1;
    return (int)b;
#endif
}

#ifdef AMIGA3DO
/* q / z for the projection without the ARM60's slow software divide:
 * q * ceil(2^31 / z) >> 31 from a table (at most 1 more than q / z,
 * truncated toward zero either way, the same for a vertex every time) */
#define RZ_N 8192
static uint32_t rz_tab[RZ_N];

static int32_t proj_div(int32_t q, int32_t z)
{
    uint32_t u, inv, al, ah, bl, bh, mid, lo, hi, ll, c1;
    int neg;
    if (z <= 0) return 0;
    if (z >= RZ_N) {                    /* shift both down: z keeps 12 bits, plenty to draw with */
        int sh = 0;
        while ((z >> sh) >= RZ_N) sh++;
        z >>= sh;
        q = q >= 0 ? q >> sh : -((-q) >> sh);
    }
    if (!rz_tab[1]) {
        int k;
        rz_tab[1] = 0x80000000UL;
        for (k = 2; k < RZ_N; k++) rz_tab[k] = (0x80000000UL + (uint32_t)k - 1) / (uint32_t)k;
    }
    inv = rz_tab[z];
    neg = q < 0;
    u = neg ? (uint32_t)0 - (uint32_t)q : (uint32_t)q;
    al = u & 0xFFFF; ah = u >> 16; bl = inv & 0xFFFF; bh = inv >> 16;
    ll = al * bl;
    mid = al * bh + ah * bl;            /* may carry: c1 */
    c1 = mid < al * bh ? 0x10000UL : 0;
    lo = ll + (mid << 16);
    hi = ah * bh + (mid >> 16) + c1 + (lo < ll ? 1 : 0);
    u = (hi << 1) | (lo >> 31);
    return neg ? -(int32_t)u : (int32_t)u;
}
#endif

int32_t a3_recip_div(int32_t q, int32_t z) { return proj_div(q, z); }

void a3_poly(const Cam *c, int32_t depth, int col, V3 *cv, int n)
{
    V3 cl[MAX_PV];
    int32_t xs[MAX_PV], ys[MAX_PV];
    DPoly *p;
    int i, b;

    n = clip_near_at(cv, n, cl, CAM_NEAR(c));
    if (n < 3) return;
    for (i = 0; i < n; i++) {
#if A3_SUB > 1
        /* x * fov fits 32 bits (as cam_project has it); the finer step comes
         * from the remainder (a 64-bit divide is a slow library call here) */
        int32_t qx = cl[i].x * c->fov, qy = cl[i].y * c->fov, z = cl[i].z;
        xs[i] = c->cx * A3_SUB + (qx / z) * A3_SUB + ((qx % z) * A3_SUB) / z;
        ys[i] = c->cyc * A3_SUB - ((qy / z) * A3_SUB + ((qy % z) * A3_SUB) / z);
#elif defined(AMIGA3DO)
        xs[i] = c->cx + proj_div(cl[i].x * c->fov, cl[i].z);
        ys[i] = c->cyc - proj_div(cl[i].y * c->fov, cl[i].z);
#else
        cam_project(c, &cl[i], &xs[i], &ys[i]);
#endif
    }
    n = clip_rect(xs, ys, n, 0, 0, c->vw * A3_SUB - 1, c->vh * A3_SUB - 1);
    if (n < 3) return;
    if (a3_npolys >= MAX_DPOLY) { a3_dropped++; return; }
    p = &pool[a3_npolys];
    p->n = (uint8_t)n;
    p->col = (uint8_t)col;
    p->next = -1;
    for (i = 0; i < n; i++) { p->x[i] = (int16_t)xs[i]; p->y[i] = (int16_t)ys[i]; }
    b = bucket_of(c->precise ? depth / 16 : depth);    /* sort buckets are in whole units */
    if (btail[b] >= 0) pool[btail[b]].next = (int16_t)a3_npolys; else bhead[b] = (int16_t)a3_npolys;
    btail[b] = (int16_t)a3_npolys;
    a3_npolys++;
}

void a3_quad(const Cam *c, int32_t depth, int col, V3 a, V3 b, V3 d, V3 e)
{
    V3 q[4];
    int32_t nz = CAM_NEAR(c);
    if (a.z < nz && b.z < nz && d.z < nz && e.z < nz) return;
    if (a.x > a.z && b.x > b.z && d.x > d.z && e.x > e.z) return;
    if (a.x < -a.z && b.x < -b.z && d.x < -d.z && e.x < -e.z) return;
    q[0] = a; q[1] = b; q[2] = d; q[3] = e;
    a3_poly(c, depth, col, q, 4);
}

#ifdef AMIGA3DO
/* ---------- the 3DO: the draw list through the cel engine ----------
 * Each polygon becomes cels of one texel (its pen's colour) mapped onto
 * quadrilaterals - a fan of quads (v0, vk, vk+1, vk+2), the last maybe a
 * triangle (a corner twice) - in the list's order, far to near. The cels
 * go over the frame (gfx_set_overlay), so what the game draws after the
 * flush goes into an overlay (16-bit, 0 = transparent) above them. */
#define MAX_CELS 3200
#define MAX_SEG  4
static CCB cels[MAX_CELS];
static int ncels;
static uint32_t pen_word[32];           /* a 1 x 1 16-bit source per pen */
static CCB *cel_tmpl;
static Canvas *frame_cv;
static uint8_t *frame_pix;
static struct { int first, count, ovl; } seg[MAX_SEG];
static int nseg;

void a3_frame_begin(Canvas *cv)
{
    int k;
    frame_cv = cv;
    frame_pix = cv->pix;
    cv->bpp = 1;
    for (k = 0; k < A3_OVL; k++)
        if (ovl_pix[k] && ovl_bands[k]) {
            int b0;
            for (b0 = 0; b0 < SCREEN_H / 16; b0++)
                if (ovl_bands[k] & (1UL << b0)) {
                    int b1 = b0;
                    while (b1 + 1 < SCREEN_H / 16 && (ovl_bands[k] & (1UL << (b1 + 1)))) b1++;
                    memset(ovl_pix[k] + b0 * 16 * SCREEN_W, 0, (size_t)((b1 - b0 + 1) * 16 * SCREEN_W * 2));
                    b0 = b1;
                }
            ovl_bands[k] = 0;
        }
    ovl_cur = -1;
    ncels = 0;
    nseg = 0;
}

extern void *gfx_layer_cel16(unsigned short *pix, int w, int h, int stride);
extern void gfx_set_overlay(void *list);

/* the next overlay for what is drawn after the 3D; 0: none left */
static int next_overlay(void)
{
    int k = ovl_cur + 1;
    if (k >= A3_OVL) return 0;
    if (!ovl_pix[k]) {
        ovl_pix[k] = (uint16_t *)malloc(SCREEN_W * SCREEN_H * 2);
        if (!ovl_pix[k]) return 0;
        memset(ovl_pix[k], 0, SCREEN_W * SCREEN_H * 2);
        ovl_cel[k] = (CCB *)gfx_layer_cel16(ovl_pix[k], SCREEN_W, SCREEN_H, SCREEN_W);
        if (!ovl_cel[k]) { free(ovl_pix[k]); ovl_pix[k] = 0; return 0; }
        ovl_cel[k]->ccb_Flags &= ~CCB_BGND;     /* 0 shows what is under it */
        ovl_cel[k]->ccb_XPos = 0;
        ovl_cel[k]->ccb_YPos = 0;
        ovl_bands[k] = 0;
    }
    ovl_cur = k;
    return 1;
}

/* game pixels to the display's 16.16 (256 lines on 240) */
#define CX(x) ((int32_t)(x) << 16)
#define CY(y) ((int32_t)(y) * 61440)

static void cel_quad(int col, int ax, int ay, int bx, int by, int cx, int cy, int dx, int dy)
{
    CCB *c;
    int32_t AX = CX(ax), AY = CY(ay), BX = CX(bx), BY = CY(by), CX_ = CX(cx), CY_ = CY(cy), DX = CX(dx), DY = CY(dy);
    if (ncels >= MAX_CELS) { a3_dropped++; return; }
    c = &cels[ncels++];
    *c = *cel_tmpl;
    c->ccb_SourcePtr = (CelData *)&pen_word[col & 31];
    c->ccb_XPos = AX;
    c->ccb_YPos = AY;
    c->ccb_HDX = (BX - AX) << 4;        /* 16.16 -> 12.20 */
    c->ccb_HDY = (BY - AY) << 4;
    c->ccb_VDX = DX - AX;
    c->ccb_VDY = DY - AY;
    c->ccb_HDDX = ((CX_ - DX) - (BX - AX)) << 4;
    c->ccb_HDDY = ((CY_ - DY) - (BY - AY)) << 4;
}

int32_t a3_stat_flush, a3_stat_cels, a3_stat_polys;   /* main_3do.c logs them */

static int cel_ready(void)
{
    int i;
    if (!cel_tmpl) {
        cel_tmpl = CreateCel(1, 1, 16, CREATECEL_UNCODED, pen_word);
        if (!cel_tmpl) return 0;
        cel_tmpl->ccb_Flags = (cel_tmpl->ccb_Flags | CCB_NPABS | CCB_SPABS | CCB_BGND) & ~CCB_LAST;
    }
    if (nseg == 0) {                    /* the pens' colours, once a frame */
        refresh_pens();
        for (i = 0; i < 32; i++) pen_word[i] = ((uint32_t)pen15[i] << 16) | pen15[i];
    }
    return 1;
}

/* a convex polygon as cels: a fan of quads */
static void cel_poly(const int16_t *x, const int16_t *y, int n, int col)
{
    int i;
    for (i = 1; i + 1 < n; i += 2) {
        int j = i + 2 < n ? i + 2 : i + 1;
        cel_quad(col, x[0], y[0], x[i], y[i], x[i + 1], y[i + 1], x[j], y[j]);
    }
}

/* the cels from first on are a segment; what is drawn next goes into an overlay above them */
static void cel_segment(int first)
{
    if (nseg > 0 && seg[nseg - 1].ovl >= 0 && !ovl_bands[seg[nseg - 1].ovl]) {
        /* nothing drawn since the last flush: these go on under the same overlay */
        seg[nseg - 1].count = ncels - seg[nseg - 1].first;
    } else if (nseg < MAX_SEG) {
        seg[nseg].first = first;
        seg[nseg].count = ncels - first;
        seg[nseg].ovl = next_overlay() ? ovl_cur : -1;
        nseg++;
        if (seg[nseg - 1].ovl >= 0) {
            frame_cv->pix = (uint8_t *)ovl_pix[ovl_cur];
            frame_cv->bpp = 2;
        }
    }
}

int32_t a3_stat_flush, a3_stat_cels, a3_stat_polys;   /* main_3do.c logs them */

static void cel_flush(void)
{
    int b, k, first = ncels;
    a3_stat_polys += a3_npolys;
    if (!cel_ready()) return;
    for (b = NBUCKET - 1; b >= 0; b--)
        for (k = bhead[b]; k >= 0; k = pool[k].next)
            cel_poly(pool[k].x, pool[k].y, pool[k].n, pool[k].col);
    cel_segment(first);
}

/* a game's own polygon list (already sorted) through the cel engine */
static int cels_first = -1;
static int cels_allowed(Canvas *cv)
{
    return cv == frame_cv && ((ovl_cur + 1 < A3_OVL && nseg < MAX_SEG) ||
                              (nseg > 0 && seg[nseg - 1].ovl >= 0 && !ovl_bands[seg[nseg - 1].ovl]));
}

int a3_cels_begin(Canvas *cv)
{
    if (!cels_allowed(cv) || !cel_ready()) return 0;
    cels_first = ncels;
    return 1;
}

void a3_cels_poly(const int16_t *xs, const int16_t *ys, int n, int col)
{
    if (cels_first >= 0) { cel_poly(xs, ys, n, col); a3_stat_polys++; }
}

void a3_cels_end(void)
{
    if (cels_first < 0) return;
    cel_segment(cels_first);
    cels_first = -1;
}

/* the 3D cels and the overlays drawn into, in order, to the layer */
void a3_frame_end(void)
{
    CCB *head = 0, *tail = 0;
    int s, k;
    for (s = 0; s < nseg; s++) {
        for (k = 0; k < seg[s].count; k++) {
            CCB *c = &cels[seg[s].first + k];
            if (tail) tail->ccb_NextPtr = c; else head = c;
            tail = c;
        }
        k = seg[s].ovl;
        if (k >= 0 && ovl_bands[k]) {
            CCB *c = ovl_cel[k];
            c->ccb_Flags |= CCB_NPABS;
            c->ccb_Flags &= ~CCB_LAST;
            if (tail) tail->ccb_NextPtr = c; else head = c;
            tail = c;
        }
    }
    if (tail) {
        tail->ccb_Flags |= CCB_LAST;
        tail->ccb_NextPtr = 0;
    }
    gfx_set_overlay(head);
    a3_stat_cels += ncels;
    if (frame_cv) { frame_cv->pix = frame_pix; frame_cv->bpp = 1; }
}
#endif

void a3_flush(Canvas *cv)
{
    int b, k;
#ifdef AMIGA3DO
    /* the frame: the cel engine, while there is an overlay left for what comes after */
    if (cels_allowed(cv)) { cel_flush(); return; }
#endif
    for (b = NBUCKET - 1; b >= 0; b--)
        for (k = bhead[b]; k >= 0; k = pool[k].next) {
            DPoly *p = &pool[k];
            raster(cv, p->x, p->y, p->n, p->col, 0, PHYS_H - 1, 0);
        }
}

/* ---------- models ---------- */
void mdl_begin(Model *m) { m->nv = 0; m->nf = 0; }

int mdl_vtx(Model *m, int x, int y, int z)
{
    int i = m->nv;
    if (i >= MDL_MAXV) return MDL_MAXV - 1;
    m->nv++;
    m->v[i][0] = (int16_t)x;
    m->v[i][1] = (int16_t)y;
    m->v[i][2] = (int16_t)z;
    return i;
}

/* Add a face; flip it if it faces the interior point (ix, iy, iz); shade
 * it by a fixed sun high in the north-west (-1, 2, 1). */
void mdl_face(Model *m, int lit, int shade, int n, int a, int b, int c, int d, int ix, int iy, int iz)
{
    Face *f;
    int16_t *A = m->v[a], *B = m->v[b], *C = m->v[c];
    int32_t ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2];
    int32_t vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
    int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    int32_t light, mag;
    if (m->nf >= MDL_MAXF) return;
    f = &m->f[m->nf++];
    f->n = (uint8_t)n;
    if (nx * (A[0] - ix) + ny * (A[1] - iy) + nz * (A[2] - iz) >= 0) {
        f->idx[0] = (uint8_t)a; f->idx[1] = (uint8_t)b; f->idx[2] = (uint8_t)c; f->idx[3] = (uint8_t)d;
    } else {
        if (n == 4) { f->idx[0] = (uint8_t)d; f->idx[1] = (uint8_t)c; f->idx[2] = (uint8_t)b; f->idx[3] = (uint8_t)a; }
        else        { f->idx[0] = (uint8_t)c; f->idx[1] = (uint8_t)b; f->idx[2] = (uint8_t)a; f->idx[3] = 0; }
        nx = -nx; ny = -ny; nz = -nz;
    }
    light = -nx + 2 * ny + nz;
    mag = (nx < 0 ? -nx : nx) + (ny < 0 ? -ny : ny) + (nz < 0 ? -nz : nz);
    f->col = (uint8_t)((mag && light * 4 > mag * 3) ? lit : shade);
}

void mdl_box(Model *m, int lit, int shade, int x0, int x1, int y0, int y1, int z0, int z1)
{
    int b = m->nv, cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, cz = (z0 + z1) / 2;
    mdl_vtx(m, x0, y0, z0); mdl_vtx(m, x1, y0, z0); mdl_vtx(m, x1, y1, z0); mdl_vtx(m, x0, y1, z0);
    mdl_vtx(m, x0, y0, z1); mdl_vtx(m, x1, y0, z1); mdl_vtx(m, x1, y1, z1); mdl_vtx(m, x0, y1, z1);
    mdl_face(m, lit, shade, 4, b + 0, b + 3, b + 2, b + 1, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 5, b + 6, b + 7, b + 4, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 4, b + 7, b + 3, b + 0, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 1, b + 2, b + 6, b + 5, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 3, b + 7, b + 6, b + 2, cx, cy, cz);
    mdl_face(m, lit, shade, 4, b + 0, b + 1, b + 5, b + 4, cx, cy, cz);
}

void mdl_pyramid(Model *m, int lit, int shade, int x0, int x1, int y0, int z0, int z1, int apex)
{
    int b = m->nv, t, cx = (x0 + x1) / 2, cz = (z0 + z1) / 2, cy = y0 + (apex - y0) / 4;
    mdl_vtx(m, x0, y0, z0); mdl_vtx(m, x1, y0, z0); mdl_vtx(m, x1, y0, z1); mdl_vtx(m, x0, y0, z1);
    t = mdl_vtx(m, cx, apex, cz);
    mdl_face(m, lit, shade, 3, b + 0, b + 1, t, 0, cx, cy, cz);
    mdl_face(m, lit, shade, 3, b + 1, b + 2, t, 0, cx, cy, cz);
    mdl_face(m, lit, shade, 3, b + 2, b + 3, t, 0, cx, cy, cz);
    mdl_face(m, lit, shade, 3, b + 3, b + 0, t, 0, cx, cy, cz);
}

void mdl_prism(Model *m, int lit, int shade, int sides, int r, int y0, int y1)
{
    int b = m->nv, k, cy = (y0 + y1) / 2;
    for (k = 0; k < sides; k++) {
        int a = (k * 65536) / sides;
        int x = (int)((a3_sin(a) * r) >> 16), z = (int)((a3_cos(a) * r) >> 16);
        mdl_vtx(m, x, y0, z);
        mdl_vtx(m, x, y1, z);
    }
    for (k = 0; k < sides; k++) {
        int j = (k + 1) % sides;
        mdl_face(m, lit, shade, 4, b + 2 * k, b + 2 * k + 1, b + 2 * j + 1, b + 2 * j, 0, cy, 0);
    }
    /* the top, as a fan of quads/triangles round the middle */
    {
        int c = mdl_vtx(m, 0, y1, 0);
        for (k = 0; k < sides; k++) {
            int j = (k + 1) % sides;
            mdl_face(m, lit, lit, 3, b + 2 * k + 1, b + 2 * j + 1, c, 0, 0, y0, 0);
        }
    }
}

void mdl_shadow(Model *m, int col, int x0, int x1, int z0, int z1)
{
    int b = m->nv;
    mdl_vtx(m, x0, 1, z0); mdl_vtx(m, x1, 1, z0); mdl_vtx(m, x1, 1, z1); mdl_vtx(m, x0, 1, z1);
    mdl_face(m, col, col, 4, b + 0, b + 1, b + 2, b + 3, 0, -10, 0);
}

void a3_rot_yaw(int32_t m[9], int yaw)
{
    int32_t c = a3_cos(yaw), s = a3_sin(yaw);
    m[0] = c;  m[1] = 0;     m[2] = s;
    m[3] = 0;  m[4] = 65536; m[5] = 0;
    m[6] = -s; m[7] = 0;     m[8] = c;
}

/* roll about z, then pitch about x, then yaw about y */
void a3_rot_ypr(int32_t m[9], int yaw, int pitch, int roll)
{
    int32_t cy = a3_cos(yaw) >> 4, sy = a3_sin(yaw) >> 4, cp = a3_cos(pitch) >> 4, sp = a3_sin(pitch) >> 4;
    int32_t cr = a3_cos(roll) >> 4, sr = a3_sin(roll) >> 4;
    /* R = Ry * Rx * Rz, each term 12.12 -> 16.16 */
    int32_t a00 = cr, a01 = -sr, a10 = (cp * sr) >> 12, a11 = (cp * cr) >> 12, a12 = -sp;
    int32_t a20 = (sp * sr) >> 12, a21 = (sp * cr) >> 12, a22 = cp;
    m[0] = (cy * a00 + sy * a20) >> 8;  m[1] = (cy * a01 + sy * a21) >> 8;  m[2] = (sy * a22) >> 8;
    m[3] = a10 << 4;                     m[4] = a11 << 4;                     m[5] = a12 << 4;
    m[6] = (-sy * a00 + cy * a20) >> 8; m[7] = (-sy * a01 + cy * a21) >> 8; m[8] = (cy * a22) >> 8;
}

void a3_model(const Cam *c, const Model *m, int32_t wx, int32_t wy, int32_t wz, const int32_t *rot,
              const uint8_t *body, int flash, int32_t depth_bias)
{
    V3 cv[MDL_MAXV], ctr;
    int32_t vis_depth[MDL_MAXF], depth;
    uint8_t vis_face[MDL_MAXF];
    int i, k, nvis = 0;

    cam_xform(c, wx, wy, wz, &ctr);
    {
        int32_t mg = 800 * CAM_U(c);
        if (ctr.z < -mg || ctr.x > ctr.z + mg || ctr.x < -ctr.z - mg) return;
    }
    for (i = 0; i < m->nv; i++) {
        int32_t x = m->v[i][0], y = m->v[i][1], z = m->v[i][2];
        if (rot) {
            int32_t rx = (x * rot[0] + y * rot[1] + z * rot[2]) >> 16;
            int32_t ry = (x * rot[3] + y * rot[4] + z * rot[5]) >> 16;
            int32_t rz = (x * rot[6] + y * rot[7] + z * rot[8]) >> 16;
            x = rx; y = ry; z = rz;
        }
        cam_xform(c, wx + x, wy + y, wz + z, &cv[i]);
    }
    /* the faces that face the camera, farthest first (a model made of
     * several parts isn't convex, so culling alone doesn't order them) */
    for (k = 0; k < m->nf; k++) {
        const Face *f = &m->f[k];
        const V3 *a = &cv[f->idx[0]], *b = &cv[f->idx[1]], *d = &cv[f->idx[2]];
        int32_t ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
        int32_t vx = d->x - a->x, vy = d->y - a->y, vz = d->z - a->z;
        int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        if (DOT3_NONNEG(nx, a->x, ny, a->y, nz, a->z)) continue;
        depth = a->z + b->z + d->z + (f->n == 4 ? cv[f->idx[3]].z : d->z);
        if (m->v[f->idx[0]][1] == 1 && m->v[f->idx[1]][1] == 1 && m->v[f->idx[2]][1] == 1)
            depth = 0x7FFFFFFF;                 /* a shadow on the ground goes first */
        for (i = nvis; i > 0 && vis_depth[i - 1] < depth; i--) {
            vis_face[i] = vis_face[i - 1];
            vis_depth[i] = vis_depth[i - 1];
        }
        vis_face[i] = (uint8_t)k;
        vis_depth[i] = depth;
        nvis++;
    }
    depth = ctr.z - depth_bias * CAM_U(c);
    for (i = 0; i < nvis; i++) {
        const Face *f = &m->f[vis_face[i]];
        V3 q[4];
        int col = f->col;
        q[0] = cv[f->idx[0]]; q[1] = cv[f->idx[1]]; q[2] = cv[f->idx[2]];
        if (f->n == 4) q[3] = cv[f->idx[3]];
        if (col >= COL_FLASH) col = flash;
        else if (col >= COL_BODY) col = body ? body[col - COL_BODY] : 3;
        a3_poly(c, depth, col, q, f->n);
    }
}
