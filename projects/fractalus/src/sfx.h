// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef FRACTALUS_SFX_H
#define FRACTALUS_SFX_H

#include <exec/types.h>

/*
 * Four pre-generated 8-bit signed procedural samples in chip RAM,
 * routed through modplay's channel-3 SFX slot. If the previous SFX
 * hasn't finished, modplay_sfx() overrides it — no queueing.
 */

enum SfxId {
    SFX_LASER      = 0,
    SFX_EXPLOSION  = 1,
    SFX_DAMAGE     = 2,
    SFX_RESCUE     = 3,
    SFX_COUNT      = 4,
};

class Sfx {
public:
    int  init();       /* 0 on success, -1 if no samples could be allocated */
    void shutdown();
    void play(int id); /* fire-and-forget; last write wins */
    void tick();       /* no-op — modplay owns Paula timing */

private:
    BYTE  *sample[SFX_COUNT];        /* chip RAM, 8-bit signed */
    UWORD  sample_len[SFX_COUNT];
    UWORD  sample_period[SFX_COUNT];
    UBYTE  opened[SFX_COUNT];        /* per-slot allocation success flag */
};

#endif
