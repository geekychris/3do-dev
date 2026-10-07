// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef VT_MODELS_H
#define VT_MODELS_H

#include "engine3d.h"

/* Precomputed ship / station models. Call vt_build_models() once
 * at startup to fill in vbase, face, and normals. */

extern Model model_cobra;      /* player / friendly */
extern Model model_krait;      /* pirate — first enemy */
extern Model model_station;    /* Coriolis cube */

void vt_build_models(void);

#endif
