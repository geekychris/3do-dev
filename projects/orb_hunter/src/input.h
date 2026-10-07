// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - Input handling (joystick + keyboard)
 */
#ifndef INPUT_H
#define INPUT_H

#include <exec/types.h>

/* Input state bits */
#define INPUT_LEFT   1
#define INPUT_RIGHT  2
#define INPUT_UP     4
#define INPUT_DOWN   8
#define INPUT_FIRE  16
#define INPUT_ESC   32
#define INPUT_START 64

/* Read joystick port 2 + keyboard state, returns OR'd INPUT_ bits */
UWORD input_read(void);

/* Call after processing IDCMP RAWKEY events */
void input_key_down(UWORD code);
void input_key_up(UWORD code);
void input_reset(void);

#endif
