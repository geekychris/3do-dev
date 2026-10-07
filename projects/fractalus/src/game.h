// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef FRACTALUS_GAME_H
#define FRACTALUS_GAME_H

#include <exec/types.h>
#include "fixed.h"

/*
 * All the mutable per-frame state for the ship + world sits here.
 * Rendering + input read this; game.cpp updates it each tick.
 */

struct ShipState {
    /* Position in world units (16 bits integer, plenty for a
     * TERRAIN_SIZE * TERRAIN_CELL_UNITS = 8192-unit world). */
    LONG x, y, z;

    /* Orientation. yaw = compass heading; pitch = nose up/down.
     * Both in ANGLE_FULL units (4096 = full circle). */
    LONG yaw;
    LONG pitch;

    /* Forward speed in world-units-per-tick. */
    LONG speed;
    LONG target_speed;

    /* Vertical velocity in world units per tick (plain integer,
     * not fixed-point — game.cpp derives it as (speed * sin(pitch))
     * shifted from the trig table's TRIG_ONE units). */
    LONG vy;
};

/* Rescue state machine — Fractalus's key gameplay loop. */
enum RescueState {
    RS_FLYING     = 0,   /* normal flight */
    RS_LANDING    = 1,   /* auto-descend + brake */
    RS_AIRLOCK    = 2,   /* "AIRLOCK CYCLING..." */
    RS_REVEAL     = 3,   /* pilot or jaggi visible */
    RS_JUMPSCARE  = 4,   /* JAGGI! red flash + shield hit */
    RS_TAKEOFF    = 5,   /* brief lift back into flying */
};

/* Top-level game mode. Restart transitions LOSE/WIN -> TITLE.
 *
 * GM_ATTRACT is an idle-screensaver-style demo: after a few seconds of
 * no input on the title screen, main.cpp drops the sim into ATTRACT
 * with a fresh world, and game.tick() synthesises always-thrust +
 * gentle-turn input so the ship flies itself in a slow circle over
 * the terrain. Any real keypress bounces us back to GM_TITLE (with
 * the triggering keystroke consumed so it doesn't also start a
 * mission on the same frame). Purely eye-candy — no scoring, no
 * combat damage. */
enum GameMode {
    GM_PLAYING = 0,
    GM_WIN     = 1,
    GM_LOSE    = 2,
    GM_TITLE   = 3,     /* pre-mission briefing, waiting for SPACE */
    GM_ATTRACT = 4,     /* auto-fly demo when title sits idle */
};

/* Mission tunables. */
#define MISSION_WIN_PILOTS   5       /* rescue N of MAX_PILOTS to win */
#define FUEL_DRAIN_FLYING    1       /* fuel units drained per N frames */
#define FUEL_DRAIN_FRAMES    6       /* -> 1000 fuel / (25fps / 6) = ~240s */

/* Global game state, allocated in main.cpp.
 *
 * Note: rescue_state / mode / running are LONG (not UBYTE / UWORD) so the
 * bridge can register them as AB_TYPE_I32 without a size mismatch — the
 * previous UBYTE registration read four bytes across adjacent struct
 * fields and returned garbage over the wire. The couple of extra bytes
 * per GameState is a rounding error. */
struct GameState {
    ShipState ship;
    ULONG     tick;
    ULONG     seed;
    LONG      running;

    /* HUD / gauges */
    LONG      fuel;              /* 0..1000 */
    LONG      shield;            /* 0..1000 */
    LONG      pilots_rescued;
    LONG      pilots_lost;       /* eaten by jaggis */
    LONG      score;

    /* Rescue sequence. */
    LONG      rescue_state;      /* RescueState */
    UWORD     state_timer;       /* frames remaining in current state */
    LONG      current_pilot;     /* -1 or index into PilotList */

    /* Mission. */
    LONG      mode;              /* GameMode */
    UBYTE     restart_pressed;   /* edge-detect SPACE in end screens */
};

/* Ship physics constants — tuned for a Fractalus-y sluggish feel. */
#define SHIP_MIN_ALTITUDE    16     /* world units above terrain */
#define SHIP_MAX_ALTITUDE    2400
#define SHIP_MAX_SPEED       32     /* world units / tick (was 48 — too fast) */
#define SHIP_ACCEL           1      /* was 2 — half-speed accel = ~2s to max */
#define SHIP_YAW_RATE        24     /* angle units / tick */
#define SHIP_PITCH_RATE      16
#define SHIP_PITCH_MAX       380    /* ~33 degrees each way */

/* Input flags (bitfield). */
#define INPUT_LEFT    0x0001
#define INPUT_RIGHT   0x0002
#define INPUT_UP      0x0004
#define INPUT_DOWN    0x0008
#define INPUT_THRUST  0x0010
#define INPUT_BRAKE   0x0020
#define INPUT_FIRE    0x0040
#define INPUT_LAND    0x0080
#define INPUT_RESTART 0x0100     /* separate from FIRE so SPACE always shoots */

class Terrain;
class PilotList;
class Combat;
class Sfx;

/* Landing detection thresholds. */
#define LAND_MAX_SPEED       6
#define LAND_MAX_ALT_ABOVE   80      /* world units above ground */
#define LAND_RADIUS          500     /* world units from a pilot to land */
/* Timer values in game ticks. Engine runs ~25 FPS after the
 * optimisation pass, so bump these so states feel like Fractalus's
 * unhurried pacing rather than a millisecond flash. */
#define AIRLOCK_FRAMES       60      /* ~2.4s */
#define REVEAL_FRAMES        40      /* ~1.6s */
#define JUMPSCARE_FRAMES     45      /* ~1.8s — long enough to see the animation */
#define TAKEOFF_FRAMES       20      /* ~0.8s */

class Game {
public:
    void init(GameState *state, Terrain *terrain, PilotList *plist,
              Combat *combat);
    void bind_sfx(Sfx *s) { sfx = s; }
    void tick(UWORD input_flags);

    const ShipState &ship() const { return gs->ship; }
    const GameState &state() const { return *gs; }
    const PilotList &pilots() const { return *pl; }
    const Combat    &combat() const { return *cb; }

private:
    GameState *gs;
    Terrain   *world;
    PilotList *pl;
    Combat    *cb;
    Sfx       *sfx;

    void update_ship(UWORD input_flags);
    void clamp_altitude();
    void update_rescue(UWORD input_flags);
    void enter_state(UBYTE new_state, UWORD frames);
};

#endif
