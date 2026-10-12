// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef VT_MODELS_H
#define VT_MODELS_H

#include "engine3d.h"

/* Precomputed ship / station models. Call vt_build_models() once
 * at startup to fill in vbase, face, and normals. */

extern Model model_skiff;      /* player / friendly */
extern Model model_raider;     /* pirate — first enemy */
extern Model model_station;    /* trade station (a cube) */

void vt_build_models(void);

#endif
