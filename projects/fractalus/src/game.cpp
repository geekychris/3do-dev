// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "game.h"
#include "terrain.h"
#include "pilots.h"
#include "combat.h"
#include "sfx.h"

extern "C" {
#include "bridge_client.h"
}

void Game::init(GameState *state, Terrain *terrain, PilotList *plist,
                Combat *combat)
{
    gs = state;
    world = terrain;
    pl = plist;
    cb = combat;

    gs->ship.x = FX16(4096);   /* middle of the world */
    gs->ship.z = FX16(4096);
    gs->ship.y = 0;             /* clamped up by update_ship on first tick */
    gs->ship.yaw = 0;
    gs->ship.pitch = 0;
    gs->ship.speed = 0;
    gs->ship.target_speed = 0;
    gs->ship.vy = 0;

    gs->tick = 0;
    gs->running = 1;
    gs->fuel = 1000;
    gs->shield = 1000;
    gs->pilots_rescued = 0;
    gs->pilots_lost = 0;
    gs->score = 0;
    gs->rescue_state = RS_FLYING;
    gs->state_timer = 0;
    gs->current_pilot = -1;
    gs->mode = GM_TITLE;       /* start at the title screen */
    gs->restart_pressed = 0;

    /* Low-flyer cruise altitude — Fractalus-style ground-hugging.
     * If a peak sticks up higher than us, the raycaster's "camera
     * inside mountain" degenerate case fires per-column (that column
     * fills with the peak's colour) — physically correct: the peak
     * is in your face. Global render still works because most
     * columns still see over lower terrain. */
    LONG ground = world->height_at_world(FX16_TOINT(gs->ship.x),
                                         FX16_TOINT(gs->ship.z));
    gs->ship.y = ground + 100;
}

void Game::update_ship(UWORD in)
{
    ShipState &s = gs->ship;

    /* Yaw */
    if (in & INPUT_LEFT)  s.yaw = (s.yaw - SHIP_YAW_RATE) & ANGLE_MASK;
    if (in & INPUT_RIGHT) s.yaw = (s.yaw + SHIP_YAW_RATE) & ANGLE_MASK;

    /* Pitch */
    if (in & INPUT_UP) {
        s.pitch += SHIP_PITCH_RATE;
        if (s.pitch > SHIP_PITCH_MAX) s.pitch = SHIP_PITCH_MAX;
    } else if (in & INPUT_DOWN) {
        s.pitch -= SHIP_PITCH_RATE;
        if (s.pitch < -SHIP_PITCH_MAX) s.pitch = -SHIP_PITCH_MAX;
    } else {
        /* Auto-level toward zero when neither pitch key held. */
        if (s.pitch > 0) s.pitch = (s.pitch > SHIP_PITCH_RATE)
                                     ? s.pitch - SHIP_PITCH_RATE : 0;
        else if (s.pitch < 0) s.pitch = (s.pitch < -SHIP_PITCH_RATE)
                                          ? s.pitch + SHIP_PITCH_RATE : 0;
    }

    /* Cruise-control throttle. Speed only changes while a key is
     * held; released, it stays where you left it.
     *   W held    -> +SHIP_ACCEL per frame, up to SHIP_MAX_SPEED
     *   S held    -> -2 per frame, down to 0 (brake)
     *   neither   -> hold current cruise speed (no drag)
     * Tap W a few times to step up incrementally, hold to go max,
     * S to slow down. */
    if (in & INPUT_THRUST) {
        s.speed += SHIP_ACCEL;
        if (s.speed > SHIP_MAX_SPEED) s.speed = SHIP_MAX_SPEED;
    } else if (in & INPUT_BRAKE) {
        s.speed -= 2;
        if (s.speed < 0) s.speed = 0;
    }
    s.target_speed = s.speed;  /* kept in sync so RS_LANDING sees it */
}

/* Look up sin/cos from the shared table in render.cpp — extern here so
 * we don't rebuild a second copy just for the ship. */
extern LONG sin_table[ANGLE_FULL];
static inline LONG isin(LONG a) { return sin_table[a & ANGLE_MASK]; }
static inline LONG icos(LONG a) { return sin_table[(a + ANGLE_QUART) & ANGLE_MASK]; }

void Game::clamp_altitude()
{
    ShipState &s = gs->ship;
    LONG ground = world->height_at_world(FX16_TOINT(s.x), FX16_TOINT(s.z));
    LONG floor  = ground + SHIP_MIN_ALTITUDE;

    if (s.y < floor) {
        s.y = floor;
        s.vy = 0;
    }
    if (s.y > SHIP_MAX_ALTITUDE) {
        s.y = SHIP_MAX_ALTITUDE;
        s.vy = 0;
    }
}

void Game::enter_state(UBYTE new_state, UWORD frames)
{
    AB_I("rescue: %ld -> %ld (timer=%ld)",
         (long)gs->rescue_state, (long)new_state, (long)frames);
    gs->rescue_state = new_state;
    gs->state_timer = frames;
}

void Game::update_rescue(UWORD in)
{
    ShipState &s = gs->ship;

    switch (gs->rescue_state) {

    case RS_FLYING: {
        /* Landing eligible if the pilot slows and drops toward the
         * ground near an active pilot. INPUT_LAND accelerates the
         * check, so the player can commit to landing deliberately. */
        LONG dist = 0;
        LONG pi = pl->find_nearest(FX16_TOINT(s.x), FX16_TOINT(s.z),
                                   LAND_RADIUS, &dist);
        LONG ground = world->height_at_world(FX16_TOINT(s.x),
                                             FX16_TOINT(s.z));
        LONG alt_above = s.y - ground;
        UWORD low_slow = (s.speed <= LAND_MAX_SPEED
                          && alt_above <= LAND_MAX_ALT_ABOVE);
        if (pi >= 0 && (low_slow || (in & INPUT_LAND))) {
            gs->current_pilot = pi;
            enter_state(RS_LANDING, 0);
        }
        break;
    }

    case RS_LANDING: {
        /* Autopilot brings us to a stop and settles onto the surface.
         * update_ship() is skipped in non-FLYING states, so we drive
         * speed and altitude directly here. */
        s.target_speed = 0;
        if (s.speed > 0) { s.speed -= 4; if (s.speed < 0) s.speed = 0; }
        LONG ground = world->height_at_world(FX16_TOINT(s.x),
                                             FX16_TOINT(s.z));
        LONG want = ground + SHIP_MIN_ALTITUDE;
        if (s.y > want) {
            s.y -= 30;
            if (s.y < want) s.y = want;
        }
        if (s.speed == 0 && s.y <= want) {
            enter_state(RS_AIRLOCK, AIRLOCK_FRAMES);
        }
        break;
    }

    case RS_AIRLOCK:
        if (gs->state_timer == 0) {
            /* Reveal: was it a real pilot or a jaggi in a suit? */
            LONG pi = gs->current_pilot;
            if (pi >= 0 && (*pl)[pi].is_jaggi) {
                (*pl)[pi].state = PILOT_JAGGI;
                gs->pilots_lost++;
                enter_state(RS_JUMPSCARE, JUMPSCARE_FRAMES);
            } else if (pi >= 0) {
                (*pl)[pi].state = PILOT_RESCUED;
                gs->pilots_rescued++;
                gs->score += 500;
                if (sfx) sfx->play(SFX_RESCUE);
                enter_state(RS_REVEAL, REVEAL_FRAMES);
            } else {
                enter_state(RS_FLYING, 0);
            }
        }
        break;

    case RS_REVEAL:
        if (gs->state_timer == 0) enter_state(RS_TAKEOFF, TAKEOFF_FRAMES);
        break;

    case RS_JUMPSCARE:
        /* Damage shield the first frame the jumpscare begins. */
        if (gs->state_timer == JUMPSCARE_FRAMES - 1) {
            gs->shield -= 200;
            if (gs->shield < 0) gs->shield = 0;
        }
        if (gs->state_timer == 0) enter_state(RS_TAKEOFF, TAKEOFF_FRAMES);
        break;

    case RS_TAKEOFF:
        /* Small automatic altitude boost so the player isn't
         * immediately stuck at ground level. */
        s.y += 3;
        if (gs->state_timer == 0) {
            gs->current_pilot = -1;
            enter_state(RS_FLYING, 0);
        }
        break;
    }

    if (gs->state_timer > 0) gs->state_timer--;
}

void Game::tick(UWORD in)
{
    /* ATTRACT mode: synthesise steady input so the ship flies itself
     * in a slow clockwise arc over the terrain, and pin shield so
     * enemy fire never ends the demo. Rest of the play body below
     * runs unchanged. Main.cpp bounces us back to GM_TITLE on any
     * real keypress. */
    if (gs->mode == GM_ATTRACT) {
        in = INPUT_THRUST | INPUT_RIGHT;
        gs->shield = 1000;
    }

    /* Non-active modes (TITLE, WIN, LOSE): freeze the world, tick a
     * timer for animations, and edge-detect the start/restart key.
     *   TITLE       SPACE begins a new mission (fire is the natural
     *               "go" key on the title screen)
     *   WIN / LOSE  RETURN begins a new mission
     * Main.cpp watches the same flag to trigger reset_world.
     * (GM_ATTRACT is deliberately NOT in this early-return set — it
     * falls through to the play body so terrain/ship keep updating.) */
    if (gs->mode != GM_PLAYING && gs->mode != GM_ATTRACT) {
        if (gs->state_timer < 65535) gs->state_timer++;
        UWORD go = (gs->mode == GM_TITLE)
            ? (in & INPUT_FIRE)      /* SPACE starts from title */
            : (in & INPUT_RESTART);  /* RETURN restarts after end */
        if (go) {
            if (!gs->restart_pressed) gs->restart_pressed = 1;
        } else {
            gs->restart_pressed = 0;
        }
        return;
    }

    /* During non-flying states the ship's own controls are locked;
     * the state machine takes over. */
    if (gs->rescue_state == RS_FLYING) {
        update_ship(in);
    } else {
        /* Auto-level pitch and hold controls neutral while landed. */
        gs->ship.pitch = 0;
    }

    ShipState &s = gs->ship;

    /* Move forward along yaw, with pitch bleeding into vertical rate. */
    if (gs->rescue_state == RS_FLYING) {
        LONG sy = isin(s.yaw);
        LONG cy = icos(s.yaw);
        LONG sp = isin(s.pitch);
        LONG horiz = (s.speed * ((LONG)TRIG_ONE - ((sp * sp) >> TRIG_SHIFT))) >> TRIG_SHIFT;
        s.x += FX16(1) * ((horiz * sy) >> TRIG_SHIFT);
        s.z += FX16(1) * ((horiz * cy) >> TRIG_SHIFT);
        s.vy = ((s.speed * sp) >> TRIG_SHIFT);
        s.y += s.vy;
        clamp_altitude();
    }

    update_rescue(in);

    /* Enemy AI + player firing + bullet motion. Only runs during
     * flight; while landed we're a sitting duck by design. */
    if (gs->rescue_state == RS_FLYING) {
        cb->tick(FX16_TOINT(s.x), s.y, FX16_TOINT(s.z), s.yaw,
                 in, &gs->shield, &gs->score);
    }

    /* Fuel drain — a fixed trickle per FUEL_DRAIN_FRAMES so a full
     * tank lasts ~4 minutes at 25 FPS. Ship still moves at fuel=0,
     * but the mission fails. */
    if ((gs->tick % FUEL_DRAIN_FRAMES) == 0 && gs->fuel > 0) {
        gs->fuel -= FUEL_DRAIN_FLYING;
        if (gs->fuel < 0) gs->fuel = 0;
    }

    /* Mission end conditions — GM_PLAYING only. ATTRACT never ends
     * from within game logic; it exits only on real player input,
     * which main.cpp handles by setting mode back to GM_TITLE. */
    if (gs->mode == GM_PLAYING) {
        if (gs->pilots_rescued >= MISSION_WIN_PILOTS) {
            gs->score += gs->fuel + gs->shield;
            gs->mode = GM_WIN;
            gs->state_timer = 0;
            gs->restart_pressed = 0;
            AB_I("MISSION COMPLETE — rescued %ld/%ld, score %ld",
                 (long)gs->pilots_rescued, (long)MISSION_WIN_PILOTS,
                 (long)gs->score);
        } else if (gs->shield <= 0) {
            gs->mode = GM_LOSE;
            gs->state_timer = 0;
            gs->restart_pressed = 0;
            AB_I("SHIELD FAILURE — score %ld", (long)gs->score);
        } else if (gs->fuel <= 0) {
            gs->mode = GM_LOSE;
            gs->state_timer = 0;
            gs->restart_pressed = 0;
            AB_I("FUEL EXHAUSTED — score %ld", (long)gs->score);
        }
    }

    gs->tick++;
}
