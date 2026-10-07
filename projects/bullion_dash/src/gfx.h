// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Bullion Dash - Graphics layer (custom screen, double buffer)
 */
#ifndef GFX_H
#define GFX_H

#include <exec/types.h>

#ifdef AMIGA3DO
/* 3DO port: implemented in gfx_3do.c on the Amiga layer, whose own
 * gfx_init()/gfx_swap() have the same names */
#define gfx_init        bd_gfx_init
#define gfx_cleanup     bd_gfx_cleanup
#define gfx_backbuffer  bd_gfx_backbuffer
#define gfx_swap        bd_gfx_swap
#define gfx_vsync       bd_gfx_vsync
#define gfx_screen      bd_gfx_screen
#define gfx_set_palette bd_gfx_set_palette
#endif
#include <graphics/rastport.h>
#include <intuition/screens.h>

/* Initialize custom screen + double buffer. Returns 0 on success. */
int gfx_init(void);

/* Cleanup screen */
void gfx_cleanup(void);

/* Get current draw rastport (back buffer) */
struct RastPort *gfx_backbuffer(void);

/* Swap buffers (display back, draw to new back) */
void gfx_swap(void);

/* Wait for vertical blank */
void gfx_vsync(void);

/* Get the screen pointer (for IDCMP window) */
struct Screen *gfx_screen(void);

/* Set palette from built-in Bullion Dash palette */
void gfx_set_palette(void);

#endif /* GFX_H */
