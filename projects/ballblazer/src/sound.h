// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef BALLBLAZER_SOUND_H
#define BALLBLAZER_SOUND_H

#include <exec/types.h>

/* Two synthesized sample slots:
 *   CLANG — long metallic hit, used on goal scored
 *   PING  — short high blip, used on ball pickup
 * Both live in chip RAM. sound_init returns FALSE if audio.device
 * can't be opened; play_* is safe to call regardless. */
BOOL sound_init(void);
void sound_play_clang(void);
void sound_play_ping(void);
void sound_cleanup(void);

#endif
