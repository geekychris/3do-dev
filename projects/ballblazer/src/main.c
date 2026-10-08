// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Ballblazer — main loop & display setup.
 *
 * Phase 1 scaffolding: opens a 320x256 5-plane double-buffered
 * screen, splits it into two 128-line panes, and renders each pane
 * from a fixed camera position at opposite ends of the pitch. Ball
 * sits at world origin. ESC quits. No gameplay yet; this exists to
 * validate the projection, split, and palette before any movement,
 * physics, or AI is layered on top.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/gfxmacros.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <string.h>
#include <stdio.h>

#include "bridge_client.h"
#include "ballblazer.h"
#ifdef AMIGA3DO
#include "floor_cels.h"
#endif
#include "sound.h"
#ifdef AMIGA3DO
#include "amiga3do.h"
#endif

#define VERSION "0.1"

/* ---- display -------------------------------------------------------- */
typedef struct {
    struct Screen        *scr;
    struct Window        *win;
    struct ScreenBuffer  *sb[2];
    struct RastPort       mrp;    /* off-screen RP that swaps bitmaps */
    struct ViewPort      *vp;
    int cur;
} Display;
static Display D;

static LONG frame_count = 0;
static LONG running = 1;
static LONG bridge_ok = 0;

/* ---- palette -------------------------------------------------------- */
static void put_rgb(struct ViewPort *vp, int pen, int r, int g, int b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
#ifdef AMIGA3DO
    (void)vp;
    gfx_set_rgb24(pen, ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b);
#else
    SetRGB32(vp, pen, (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24);
#endif
}

static void set_palette(struct ViewPort *vp)
{
    put_rgb(vp, PEN_BLACK,      0,   0,   0);
    put_rgb(vp, PEN_SKY_TOP,   30,  20,  90);   /* P2 pane sky: deep blue */
    put_rgb(vp, PEN_SKY_BOT,  110,  60, 130);   /* P1 pane sky: magenta */
    put_rgb(vp, PEN_GRID_A,    20,  20,  50);   /* checker cell A: dark blue-black */
    put_rgb(vp, PEN_GRID_B,   180, 180, 200);   /* checker cell B: pale grey */
    put_rgb(vp, PEN_GRID_LINE, 50,  50,  90);   /* grid seam colour */
    put_rgb(vp, PEN_GOAL_P1,   40, 200, 255);   /* P1 goal cyan */
    put_rgb(vp, PEN_GOAL_P2,  255, 180,  40);   /* P2 goal amber */
    put_rgb(vp, PEN_BALL_DARK, 90,  10,  10);   /* ball terminator */
    put_rgb(vp, PEN_BALL,     220,  40,  30);   /* ball mid red */
    put_rgb(vp, PEN_BALL_HI,  255, 200, 160);   /* ball specular hint */
    put_rgb(vp, PEN_ROTO_P1,   40, 200, 255);   /* P1 rotofoil body cyan */
    put_rgb(vp, PEN_ROTO_P2,  255, 180,  40);   /* P2 rotofoil body amber */
    put_rgb(vp, PEN_ROTO_EDGE, 20,  20,  40);   /* rotofoil outline */
    put_rgb(vp, PEN_HUD_BG,    10,  10,  25);
    put_rgb(vp, PEN_HUD_FG,   220, 220, 240);
    put_rgb(vp, PEN_WHITE,    255, 255, 255);
}

/* ---- display open/close -------------------------------------------- */
static void close_display(void);
#ifdef AMIGA3DO
/* 3DO port: 32-colour screen + double buffering from the sdk/amiga layer */
static int open_display(void)
{
    if (!gfx_init(0, 32))
        return 1;
    set_palette(0);
    /* the floor is drawn by the cel engine under the frame (pitch.c) */
    if (fc_init(gfx_pen_rgb16(PEN_GRID_A), gfx_pen_rgb16(PEN_GRID_B)))
        gfx_set_transparent_pen(PEN_FLOOR_HW);
    return 0;
}
static void close_display(void) { gfx_exit(); }
#else
static int open_display(void)
{
    ULONG modeID = LORES_KEY;
    D.scr = OpenScreenTags(NULL,
                           SA_Width,     SCR_W,
                           SA_Height,    SCR_H,
                           SA_Depth,     SCR_DEPTH,
                           SA_DisplayID, modeID,
                           SA_Type,      CUSTOMSCREEN,
                           SA_Quiet,     TRUE,
                           SA_ShowTitle, FALSE,
                           TAG_END);
    if (!D.scr) { AB_E("OpenScreen failed"); return 1; }
    D.vp = &D.scr->ViewPort;

    D.win = OpenWindowTags(NULL,
                           WA_CustomScreen, (ULONG)D.scr,
                           WA_Width,        SCR_W,
                           WA_Height,       SCR_H,
                           WA_Left,         0,
                           WA_Top,          0,
                           WA_Backdrop,     TRUE,
                           WA_Borderless,   TRUE,
                           WA_Activate,     TRUE,
                           WA_RMBTrap,      TRUE,
                           WA_IDCMP,        IDCMP_RAWKEY | IDCMP_MOUSEMOVE,
                           TAG_END);
    if (!D.win) { AB_E("OpenWindow failed"); close_display(); return 1; }

    D.sb[0] = AllocScreenBuffer(D.scr, NULL, SB_SCREEN_BITMAP);
    D.sb[1] = AllocScreenBuffer(D.scr, NULL, 0);
    if (!D.sb[0] || !D.sb[1]) { AB_E("AllocScreenBuffer failed"); close_display(); return 1; }
    D.cur = 0;

    InitRastPort(&D.mrp);
    D.mrp.Layer = NULL;

    set_palette(D.vp);
    return 0;
}
static void close_display(void)
{
    if (D.sb[1]) { FreeScreenBuffer(D.scr, D.sb[1]); D.sb[1] = NULL; }
    if (D.sb[0]) { FreeScreenBuffer(D.scr, D.sb[0]); D.sb[0] = NULL; }
    if (D.win)   { CloseWindow(D.win); D.win = NULL; }
    if (D.scr)   { CloseScreen(D.scr); D.scr = NULL; }
}
#endif

/* ---- gameplay state ------------------------------------------------ */
static Rotofoil p1, p2;
static Ball     ball;
static int p1_score = 0, p2_score = 0;
static LONG match_frames_left;      /* countdown, 25fps assumption */
#define MATCH_FRAMES (90L * 25)     /* 90 seconds */
static int last_scorer = -1;        /* 0 = P1, 1 = P2, -1 = none yet */
static int tackle_cooldown = 0;     /* frames until next tackle allowed */
#define TACKLE_COOLDOWN_F   60      /* ~0.7s at 88fps observed */

/* WASD input state (edge-latched: pressed while down, cleared on up). */
static int in_fwd = 0, in_back = 0, in_left = 0, in_right = 0;

static void kickoff(void)
{
    p1.x = -PITCH_LENGTH * 3 / 4;  p1.z = 0;
    p1.vx = 0; p1.vz = 0;  p1.angle = 0;   p1.has_ball = 0;
    p2.x =  PITCH_LENGTH * 3 / 4;  p2.z = 0;
    p2.vx = 0; p2.vz = 0;  p2.angle = 180; p2.has_ball = 0;
    ball.x = 0;  ball.z = 0;  ball.vx = 0; ball.vz = 0; ball.carrier = -1;
}

static void init_game(void)
{
    p1_score = 0; p2_score = 0;
    match_frames_left = MATCH_FRAMES;
    last_scorer = -1;
    kickoff();
}

/* Award points when the ball crosses a goal line. Distance from the
 * scoring rotofoil's start position determines the bonus — the
 * classic Ballblazer 1/2/3-point ladder based on how far you had to
 * shoot from. Returns nonzero when a goal was scored. */
static int check_goal(void)
{
    /* Only free-flying or ball-carrying counts; if ball is carried
     * across the goal line, treat that as scoring too. */
    Rotofoil *carrier = NULL;
    if (ball.carrier == 0) carrier = &p1;
    if (ball.carrier == 1) carrier = &p2;

    /* P2's goal at +PITCH_LENGTH: P1 scores here. */
    if (ball.x >= PITCH_LENGTH) {
        int pts = 1;
        if (carrier == &p1) {
            LONG dist = PITCH_LENGTH - p1.x;   /* how far to opp goal */
            pts = (dist > PITCH_LENGTH * 3 / 4) ? 3 :
                  (dist > PITCH_LENGTH     / 2) ? 2 : 1;
        }
        p1_score += pts;
        last_scorer = 0;
        AB_I("P1 scores %ld (total %ld)", (long)pts, (long)p1_score);
        sound_play_clang();
        kickoff();
        return 1;
    }
    if (ball.x <= -PITCH_LENGTH) {
        int pts = 1;
        if (carrier == &p2) {
            LONG dist = p2.x - (-PITCH_LENGTH);
            pts = (dist > PITCH_LENGTH * 3 / 4) ? 3 :
                  (dist > PITCH_LENGTH     / 2) ? 2 : 1;
        }
        p2_score += pts;
        last_scorer = 1;
        AB_I("P2 scores %ld (total %ld)", (long)pts, (long)p2_score);
        sound_play_clang();
        kickoff();
        return 1;
    }
    return 0;
}

/* Integer atan2 in degrees (0..359), only accurate to a few degrees
 * — plenty for auto-facing another point on the pitch. Uses the same
 * sin table via a coarse binary-search over the unit circle. */
static LONG atan2_deg(LONG dy, LONG dx)
{
    LONG best_a, best_d;
    LONG a;
    if (dx == 0 && dy == 0) return 0;
    best_a = 0; best_d = 0x7FFFFFFFL;
    for (a = 0; a < 360; a += 3) {
        LONG cx = math_cos(a), cy = math_sin(a);
        /* Angle-of-vector: rotate a unit vector by -a and see if it
         * ends up close to (+X, 0). Equivalent: dot with (cos a, sin a)
         * should equal magnitude; cross should be ~0. Distance in
         * cross^2 space. */
        LONG cross = ((dx >> 8) * (cy >> 8) - (dy >> 8) * (cx >> 8));
        LONG dot   = ((dx >> 8) * (cx >> 8) + (dy >> 8) * (cy >> 8));
        LONG err;
        if (dot < 0) continue;   /* keep vector in the +forward half */
        err = cross < 0 ? -cross : cross;
        if (err < best_d) { best_d = err; best_a = a; }
    }
    return best_a;
}

/* Rotofoil physics — one Euler step per frame. */
#define ACCEL       ((LONG)(ONE / 20))     /* WASD accel per frame */
#define STRAFE      ((LONG)(ONE / 25))
#define FRICTION_N  15                     /* keeps 15/16 of velocity */
#define FRICTION_D  16
#define V_CAP       (ONE * 2)              /* max 2 world units/frame */

static void update_rotofoil(Rotofoil *r, int fwd, int back, int lft, int rgt,
                            const Ball *b)
{
    LONG ca;
    LONG sa;
    LONG af, ar;
    LONG ax;
    LONG az;
    LONG mag2;
    /* In the original Ballblazer each rotofoil ALWAYS faces its
     * opponent's goal — P1's angle is fixed at 0 (+X), P2's at 180
     * (-X). No rotation. WASD is direct world-axis movement:
     *   forward = toward opponent's goal (sign of camera forward)
     *   strafe  = sideways along Z, sign matches on-screen direction. */
    (void)b;
    ca = math_cos(r->angle); /* +1 for P1, -1 for P2 */
    sa = math_sin(r->angle); /* 0 for both */
    af = 0; ar = 0;
    if (fwd)  af += ACCEL;
    if (back) af -= ACCEL;
    if (rgt)  ar += STRAFE;
    if (lft)  ar -= STRAFE;
    ax = ((af >> 8) * (ca >> 8) + (ar >> 8) * (sa >> 8));
    az = ((af >> 8) * (sa >> 8) - (ar >> 8) * (ca >> 8));
    r->vx += ax;
    r->vz += az;

    /* Friction. */
    r->vx = (r->vx * FRICTION_N) / FRICTION_D;
    r->vz = (r->vz * FRICTION_N) / FRICTION_D;

    /* Velocity cap. */
    mag2 = ((r->vx >> 8) * (r->vx >> 8) + (r->vz >> 8) * (r->vz >> 8));
    if (mag2 > (V_CAP >> 8) * (V_CAP >> 8)) {
        /* rough clamp — scale down until under cap; two iters plenty */
        r->vx = r->vx * 3 / 4;
        r->vz = r->vz * 3 / 4;
    }

    r->x += r->vx;
    r->z += r->vz;

    /* Pitch bounds — clamp to inside the sidelines and beyond goals. */
    if (r->x >  PITCH_LENGTH) { r->x =  PITCH_LENGTH; r->vx = 0; }
    if (r->x < -PITCH_LENGTH) { r->x = -PITCH_LENGTH; r->vx = 0; }
    if (r->z >  PITCH_WIDTH)  { r->z =  PITCH_WIDTH;  r->vz = 0; }
    if (r->z < -PITCH_WIDTH)  { r->z = -PITCH_WIDTH;  r->vz = 0; }
}

/* Ball physics. If carried, sits just in front of carrier; else
 * coasts with a light drag. */
#define BALL_FRICTION_N  30
#define BALL_FRICTION_D  32
#define PICKUP_DIST      (3 * ONE)        /* touch radius */
#define ROTO_RADIUS      (2 * ONE)        /* rotofoil collision radius */

/* Resolve rotofoil-vs-rotofoil collision. Rotofoils inside 2×ROTO_RADIUS
 * bump apart along the separation vector with an elastic 1D bounce.
 * If one of them is the ball carrier and the tackle cooldown has
 * elapsed, ownership transfers to the tackler at the moment of contact
 * — this is the ONLY tackle path now (ball-proximity checks can't fire
 * on head-on approach because the ball sits on the far side of the
 * carrier, always outside the attacker's pickup radius). */
static void resolve_collision(Rotofoil *a, Rotofoil *b)
{
    LONG dx = b->x - a->x;
    LONG dz = b->z - a->z;
    LONG sep2 = ((dx >> 8)*(dx >> 8) + (dz >> 8)*(dz >> 8));
    LONG min = (ROTO_RADIUS * 2) >> 8;
    LONG min2 = min * min;
    LONG dist;
    LONG nx;
    LONG nz;
    LONG overlap_q8;
    LONG push_x;
    LONG push_z;
    LONG rel_vx;
    LONG rel_vz;
    LONG rel_vn;
    LONG impulse_x;
    LONG impulse_z;
    if (sep2 >= min2) return;

    /* Ball transfer on contact. a = P1 (idx 0), b = P2 (idx 1).
     * On successful tackle the new carrier also gets an impulse kick
     * in their forward direction — without it, both rotofoils stay
     * locked in the collision zone forever with the ball ping-ponging
     * every cooldown expiry. */
    if (tackle_cooldown == 0 && ball.carrier >= 0) {
        int steal_to = (ball.carrier == 0) ? 1 : 0;
        Rotofoil *new_carrier = (steal_to == 0) ? a : b;
        Rotofoil *old_carrier = (steal_to == 0) ? b : a;
        LONG kca;
        LONG ksa;
        old_carrier->has_ball = 0;
        new_carrier->has_ball = 1;
        ball.carrier    = steal_to;
        tackle_cooldown = TACKLE_COOLDOWN_F;
        sound_play_ping();
        AB_I("tackle: ball -> %s", steal_to == 0 ? "P1" : "P2");
        /* Kick the new carrier forward, and give the old carrier a
         * matching backwards kick so they briefly disengage. */
        kca = math_cos(new_carrier->angle);
        ksa = math_sin(new_carrier->angle);
        new_carrier->vx += kca;      /* +1 world unit / frame in facing dir */
        new_carrier->vz += ksa;
        old_carrier->vx -= kca / 2;
        old_carrier->vz -= ksa / 2;
    }

    /* Integer distance ~sqrt(sep2), fall back to 1 to avoid /0. */
    dist = 1;
    while ((dist + 1) * (dist + 1) <= sep2) dist++;
    if (dist < 1) dist = 1;

    /* Unit vector components in Q8. */
    nx = ((dx >> 8) * 256) / dist;
    nz = ((dz >> 8) * 256) / dist;
    overlap_q8 = min - dist;
    push_x = (overlap_q8 * nx) >> 1;
    push_z = (overlap_q8 * nz) >> 1;
    a->x -= push_x;
    a->z -= push_z;
    b->x += push_x;
    b->z += push_z;

    /* Velocity exchange along collision normal; only if approaching. */
    rel_vx = b->vx - a->vx;
    rel_vz = b->vz - a->vz;
    rel_vn = ((rel_vx >> 8) * nx + (rel_vz >> 8) * nz);
    if (rel_vn >= 0) return;
    impulse_x = (-rel_vn * nx) >> 8;
    impulse_z = (-rel_vn * nz) >> 8;
    a->vx -= impulse_x >> 1;
    a->vz -= impulse_z >> 1;
    b->vx += impulse_x >> 1;
    b->vz += impulse_z >> 1;
}

static void update_ball(void)
{
    if (ball.carrier < 0) {
        /* Free ball — check for pickup by either rotofoil. */
        LONG d1x = ball.x - p1.x, d1z = ball.z - p1.z;
        LONG d2x = ball.x - p2.x, d2z = ball.z - p2.z;
        LONG r2  = ((PICKUP_DIST >> 8) * (PICKUP_DIST >> 8));
        LONG dd1 = ((d1x >> 8) * (d1x >> 8) + (d1z >> 8) * (d1z >> 8));
        LONG dd2 = ((d2x >> 8) * (d2x >> 8) + (d2z >> 8) * (d2z >> 8));
        if (dd1 < r2 && dd1 <= dd2) { ball.carrier = 0; p1.has_ball = 1; sound_play_ping(); }
        else if (dd2 < r2)          { ball.carrier = 1; p2.has_ball = 1; sound_play_ping(); }
        /* coast */
        ball.x += ball.vx;
        ball.z += ball.vz;
        ball.vx = (ball.vx * BALL_FRICTION_N) / BALL_FRICTION_D;
        ball.vz = (ball.vz * BALL_FRICTION_N) / BALL_FRICTION_D;
    } else {
        /* Carried — sit 2 world units in front of the carrier. */
        Rotofoil *carrier = (ball.carrier == 0) ? &p1 : &p2;
        Rotofoil *other   = (ball.carrier == 0) ? &p2 : &p1;
        LONG ca = math_cos(carrier->angle), sa = math_sin(carrier->angle);
        /* ca/sa are already ONE-scaled, so 2 world units in front of
         * the carrier is just (2*ca, 2*sa). Do NOT pre-multiply by ONE
         * — that overflows a 32-bit LONG (2 * 65536 * 65536 = 2^33). */
        ball.x = carrier->x + (2L * ca);
        ball.z = carrier->z + (2L * sa);
        ball.vx = 0; ball.vz = 0;

        /* Tackle handled by resolve_collision(); nothing to do here. */
        (void)other;
    }
    /* Sidelines clamp — but goal-line crossings are goals, so DON'T
     * clamp X here. check_goal() runs right after and either fires
     * a goal + kickoff or (never happens in practice) leaves the ball
     * beyond the goal line. */
    if (ball.z >  PITCH_WIDTH) { ball.z =  PITCH_WIDTH;  ball.vz = -ball.vz; }
    if (ball.z < -PITCH_WIDTH) { ball.z = -PITCH_WIDTH;  ball.vz = -ball.vz; }
}

/* ---- frame loop --------------------------------------------------- */
static void draw_hud(struct RastPort *rp)
{
    char buf[32];
    LONG secs = (match_frames_left + 24) / 25;
    if (secs < 0) secs = 0;

    SetAPen(rp, PEN_HUD_FG);
    SetBPen(rp, PEN_HUD_BG);
    SetDrMd(rp, JAM2);

    /* Top pane: P2 score top-left, timer top-centre. Suffix "*" marks
     * whoever currently carries the ball. */
    Move(rp, 4, 10);
    sprintf(buf, "P2 CPU: %ld %s", (long)p2_score,
            (ball.carrier == 1) ? "*BALL*" : "");
    Text(rp, (STRPTR)buf, strlen(buf));

    Move(rp, SCR_W / 2 - 20, 10);
    sprintf(buf, "%02ld:%02ld", (long)(secs / 60), (long)(secs % 60));
    Text(rp, (STRPTR)buf, strlen(buf));

    /* Bottom pane: P1 score and ball marker. */
    Move(rp, 4, PANE_H + 10);
    sprintf(buf, "P1 YOU: %ld %s", (long)p1_score,
            (ball.carrier == 0) ? "*BALL*" : "");
    Text(rp, (STRPTR)buf, strlen(buf));

    /* Match-over banner. */
    if (match_frames_left <= 0) {
        const char *winner =
            (p1_score >  p2_score) ? "YOU WIN!" :
            (p2_score >  p1_score) ? "CPU WINS" : "DRAW";
        Move(rp, SCR_W/2 - 30, PANE_H - 6);
        Text(rp, (STRPTR)winner, strlen(winner));
    }
}

static void render_frame(void)
{
#ifdef AMIGA3DO
    struct RastPort *rp = gfx_back();
#else
    struct BitMap *bm = D.sb[D.cur]->sb_BitMap;
    struct RastPort *rp = &D.mrp;
    rp->BitMap = bm;
#endif

#ifdef AMIGA3DO
    fc_begin();
#endif
    /* P1 (human) pane sees the P2 rotofoil. */
    pitch_render(rp, PANE_P1_Y0, p1.x, p1.z, p1.angle, &ball,
                 p2.x, p2.z, PEN_ROTO_P2);
    /* P2 (CPU) pane sees the P1 rotofoil. */
    pitch_render(rp, PANE_P2_Y0, p2.x, p2.z, p2.angle, &ball,
                 p1.x, p1.z, PEN_ROTO_P1);

    /* Mid-screen divider (2 px black). */
    SetAPen(rp, PEN_BLACK);
    RectFill(rp, 0, PANE_H - 1, SCR_W - 1, PANE_H);

    draw_hud(rp);

#ifdef AMIGA3DO
    gfx_set_underlay(fc_list());
    gfx_swap();             /* paces to 50 fps */
#else
    WaitBlit();
    if (ChangeScreenBuffer(D.scr, D.sb[D.cur])) {
        WaitTOF();
        D.cur ^= 1;
    } else {
        WaitTOF();
    }
#endif
}

int main(void)
{
    BOOL sound_ok;
#ifndef AMIGA3DO
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (!IntuitionBase) return 1;
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!GfxBase) { CloseLibrary((struct Library *)IntuitionBase); return 1; }
#endif

    printf("ballblazer v%s\n", VERSION);
    if (ab_init("BALL") != 0) { printf("  Bridge: NOT FOUND\n"); bridge_ok = 0; }
    else { printf("  Bridge: CONNECTED\n"); bridge_ok = 1; }
    AB_I("ballblazer v%s starting", VERSION);
    ab_register_var("frame_count", AB_TYPE_I32, &frame_count);
    ab_register_var("running",     AB_TYPE_I32, &running);
    ab_register_var("p1_score",    AB_TYPE_I32, &p1_score);
    ab_register_var("p2_score",    AB_TYPE_I32, &p2_score);
    ab_register_var("time_left",   AB_TYPE_I32, &match_frames_left);
    ab_register_var("p1_x",        AB_TYPE_I32, &p1.x);
    ab_register_var("p1_z",        AB_TYPE_I32, &p1.z);
    ab_register_var("p2_x",        AB_TYPE_I32, &p2.x);
    ab_register_var("p2_z",        AB_TYPE_I32, &p2.z);
    ab_register_var("carrier",     AB_TYPE_I32, &ball.carrier);
    ab_register_var("tackle_cd",   AB_TYPE_I32, &tackle_cooldown);

    math_init();
    init_game();
    sound_ok = sound_init();
    if (!sound_ok) AB_W("audio.device unavailable — goals will be silent");

    if (open_display()) {
        ab_cleanup();
#ifndef AMIGA3DO
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
#endif
        return 1;
    }

    while (running) {
        LONG tgt_x, tgt_z;
        LONG dx_t;
        LONG dz_t;
        int cpu_fwd, cpu_back, cpu_lft, cpu_rgt;
#ifdef AMIGA3DO
        /* 3DO port: D-pad = W/A/S/D (forward/left/back/right), X quits */
        {
            ULONG held = pad_held(0);
            in_fwd   = (held & (PAD_UP | PAD_A)) ? 1 : 0;
            in_back  = (held & (PAD_DOWN | PAD_B)) ? 1 : 0;
            in_left  = (held & PAD_LEFT) ? 1 : 0;
            in_right = (held & PAD_RIGHT) ? 1 : 0;
            if (amiga_quit_requested()) { running = 0; break; }
        }
#else
        struct IntuiMessage *msg;
        ULONG sig = SetSignal(0L, 0L);
        if (sig & SIGBREAKF_CTRL_C) { running = 0; break; }

        while ((msg = (struct IntuiMessage *)GetMsg(D.win->UserPort))) {
            ULONG cls  = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_RAWKEY) {
                /* Raw key codes have bit 7 set on release. */
                int down = !(code & 0x80);
                int c    = code & 0x7F;
                if (c == 0x45 && down) running = 0;   /* ESC */
                else if (c == 0x11) in_fwd   = down;  /* W */
                else if (c == 0x21) in_back  = down;  /* S */
                else if (c == 0x20) in_left  = down;  /* A */
                else if (c == 0x22) in_right = down;  /* D */
            }
        }
#endif

#ifdef AMIGA3DO
        /* 3DO port: game logic at 50 steps/s (gfx_steps); the two 3D
         * panes are drawn once per frame */
        { int steps_ = gfx_steps(); while (steps_-- > 0) {
#endif
        /* CPU strategy with fixed facing:
         *   - Target: ball position (chase) or P1 goal (when carrying).
         *   - P2 faces -X (angle 180) so its "forward" = -X (toward P1).
         *   - Pick fwd/back based on X-delta to target, strafe based on
         *     Z-delta. Small deadband so it doesn't jitter. */
        if (p2.has_ball) { tgt_x = -PITCH_LENGTH; tgt_z = 0; }
        else             { tgt_x = ball.x;        tgt_z = ball.z; }
        dx_t = tgt_x - p2.x;
        dz_t = tgt_z - p2.z;
        cpu_fwd = 0; cpu_back = 0; cpu_lft = 0; cpu_rgt = 0;
        /* P2 faces -X: "forward" = -X, so we want to go forward when
         * target is more negative in X than us (dx_t < -threshold). */
        if (dx_t < -ONE) cpu_fwd  = 1;
        if (dx_t >  ONE) cpu_back = 1;
        /* Strafe: P2's screen-right corresponds to +Z (since angle=180,
         * right vector = (sin 180, -cos 180) = (0, +1)). Move toward tgt_z. */
        if (dz_t >  ONE) cpu_rgt = 1;
        if (dz_t < -ONE) cpu_lft = 1;

        update_rotofoil(&p1, in_fwd, in_back, in_left, in_right, &ball);
        update_rotofoil(&p2, cpu_fwd, cpu_back, cpu_lft, cpu_rgt, &ball);
        resolve_collision(&p1, &p2);
        update_ball();
        check_goal();

        if (tackle_cooldown > 0)   tackle_cooldown--;
        if (match_frames_left > 0) match_frames_left--;
#ifdef AMIGA3DO
        } }
#endif

        {   /* log score changes for tests and the debug console */
            static int last_p1 = -1, last_p2 = -1;
            if (p1_score != last_p1 || p2_score != last_p2) {
                AB_I("score=%ld:%ld time=%ld", (long)p1_score, (long)p2_score, (long)match_frames_left);
                last_p1 = p1_score;
                last_p2 = p2_score;
            }
        }
        render_frame();
        frame_count++;
        if (bridge_ok) ab_poll();
    }

    AB_I("ballblazer shutting down (%ld frames)", (long)frame_count);
    close_display();
    sound_cleanup();
    ab_cleanup();
#ifndef AMIGA3DO
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
#endif
    printf("ballblazer finished.\n");
    return 0;
}
