// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* render.h - Terra Strike renderer
 *
 * It draws into a Canvas: on the classic Amiga a RastPort on the
 * five-bitplane screen buffer, on AmigaOS 4 a 320x256 buffer of 8-bit pens
 * that main_os4.c turns into RGB. */
#ifndef RENDER_H
#define RENDER_H

#include "ts.h"

#define SCREEN_W 320
#define SCREEN_H 256

#if defined(__PPC__) || defined(AMIGA3DO)   /* the 3DO: sdk/amiga's frame of pens */
typedef struct { uint8_t *pix; } Canvas;    /* SCREEN_W x SCREEN_H pens */
#else
#include <graphics/rastport.h>
typedef struct RastPort Canvas;
#endif

typedef struct { int32_t tiles, models, polys, dropped; } RenderStats;
extern RenderStats render_stats;

int  render_init(void);
#if !defined(__PPC__) && !defined(AMIGA3DO)
void render_attach(struct RastPort *rp);
#endif
void render_cleanup(void);
void camera_follow(Cam *c, const World *w);
void render_build(const World *w, Cam *c);   /* transform, clip, sort */
void render_draw(Canvas *cv);                /* fill the sorted polygons */
void render_hud(Canvas *cv, int buf, const World *w, int fps, int paused);
void render_hud_reset(void);
void render_title(Canvas *cv, const World *w);
void render_title_image(Canvas *cv, int frame);   /* main screen artwork */

#endif
