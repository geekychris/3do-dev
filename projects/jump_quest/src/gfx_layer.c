/*
 * JUMP QUEST - 3DO port: calls into the sdk/amiga layer for gfx.c, kept in
 * a separate file because game.h renames the game's own gfx_* functions.
 */
#include <exec/types.h>
#include "amiga3do.h"

int  jq_layer_init(const UWORD *pal, int n) { return gfx_init(pal, n); }
void jq_layer_exit(void)                    { gfx_exit(); }
void jq_layer_swap(void)                    { gfx_swap(); }
struct RastPort *jq_layer_back(void)        { return gfx_back(); }
