// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* ace.h - Ace Pilot 2's game shell, shared by game.c and render.c */
#ifndef ACE_H
#define ACE_H

#include "a3d_draw.h"
#include "ap.h"

enum { AS_TITLE, AS_BRIEF, AS_PLAY, AS_RESULT, AS_OVER };
enum { VIEW_CHASE, VIEW_COCKPIT };

#define MAX_BLASTS 12
#define MAX_PUFFS  40
typedef struct { int t, size, pen; int32_t x, y, z; } Blast;   /* an explosion: world units */
typedef struct { int t, size; int32_t x, y, z; } Puff;          /* smoke */

typedef struct {
    int state, state_timer;
    int start_mission, autoplay, paused, help, view, invert;
    char flash[40];
    int flash_timer, flash_pen;
    int hurt_timer;
    int32_t best;
    int result;                         /* the last mission: PH_DONE or PH_FAILED */
    int32_t m_score, m_kills, m_gkills, m_shots, m_hits;   /* at the mission's start */
    int cam_yaw;                        /* the chase camera eases round after the plane */
    Blast blast[MAX_BLASTS];
    Puff puff[MAX_PUFFS];
    int next_puff;
} Ace;

extern Ace ac;
extern APGame game;
extern int32_t view_range;

void render_init(void);
void render_game(Canvas *cv, const APGame *g);
void render_title(Canvas *cv, const APGame *g);
void render_brief(Canvas *cv, const APGame *g);
void render_help(Canvas *cv, int page);
extern const uint16_t ace_palette[32];

#endif
