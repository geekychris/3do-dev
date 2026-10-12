// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef SOUND_H
#define SOUND_H

int  sound_init(void);
void sound_cleanup(void);
void sound_update(void);

/* Sound triggers */
void sound_seed(void);
void sound_eat_spider(void);
void sound_die(void);
void sound_bonus(void);
void sound_extra_life(void);
void sound_power(void);
/* Stop the power-pellet drone: clears power_on and disables channel 3. */
void sound_power_off(void);

/* Pulse control */
void sound_set_pulse(int speed);
void sound_pulse_off(void);

#endif
