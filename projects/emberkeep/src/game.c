/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#include "game.h"
#include "levels.h"

#include <string.h>

/*
 * Game rules only - no rendering / I/O / bridge chatter. tick(input)
 * mutates *gs; main calls it once per frame + reads back state
 * for rendering.
 *
 * 3DO port: game.cpp's EmberGame methods as C functions; the rules are
 * unchanged (references became pointers, bool became int).
 */

static GameState *gs;

static void spawn_player(void);

void game_init(GameState *state)
{
    gs = state;
    memset(gs, 0, sizeof(*gs));
    gs->mode = GM_TITLE;
    gs->lives = 3;
    gs->level_index = 0;
    gs->lantern = LANTERN_MAX;
}

void game_load_level(LONG index)
{
    const LevelSpec *L;
    const EnemySpec *e;
    LONG row, col;

    if (index < 0 || index >= NUM_LEVELS) index = 0;
    L = &LEVELS[index];

    memset(gs->grid, 0, sizeof(gs->grid));
    memset(gs->enemies, 0, sizeof(gs->enemies));
    gs->num_enemies = 0;
    gs->runes_total = 0;
    gs->runes_remaining = 0;

    for (row = 0; row < GRID_H; row++) {
        for (col = 0; col < GRID_W; col++) {
            char c = L->tiles[row * GRID_W + col];
            UBYTE k = T_FLOOR;
            switch (c) {
                case '#': k = T_WALL; break;
                case 'R': k = T_RUNE; gs->runes_total++; gs->runes_remaining++; break;
                case 'E': k = T_PORTAL; break;
                case 'O': k = T_OIL; break;
                case '^': k = T_HAZARD; break;
                case 'P':
                    gs->spawn_x = col * TILE_W;
                    gs->spawn_y = row * TILE_H;
                    k = T_FLOOR;
                    break;
                default:  k = T_FLOOR; break;
            }
            gs->grid[row][col] = k;
        }
    }

    e = L->enemies;
    while (e && e->axis != 0xFF && gs->num_enemies < MAX_ENEMIES) {
        Enemy *E = &gs->enemies[gs->num_enemies++];
        E->axis    = e->axis;
        E->colour  = e->colour;
        E->dx      = e->dx;
        E->dy      = e->dy;
        E->x       = e->x;
        E->y       = e->y;
        E->min_pos = e->min_pos;
        E->max_pos = e->max_pos;
        E->alive   = 1;
        E->frame   = 0;
        e++;
    }

    gs->lantern = LANTERN_MAX;
    spawn_player();
}

void game_restart_current(void) { game_load_level(gs->level_index); }

static void spawn_player(void)
{
    Player *p = &gs->player;
    p->x = gs->spawn_x;
    p->y = gs->spawn_y;
    p->facing = 2;   /* face down */
    p->frame = 0;
}

static UBYTE tile_at_pixel(LONG px, LONG py)
{
    if (px < 0 || py < 0 || px >= PLAY_W || py >= PLAY_H) return T_WALL;
    return gs->grid[py / TILE_H][px / TILE_W];
}

static int wall_at(LONG px, LONG py)
{
    return tile_at_pixel(px, py) == T_WALL;
}

static int touching_enemy(void)
{
    LONG px1 = gs->player.x + 2, py1 = gs->player.y + 2;
    LONG px2 = gs->player.x + 13, py2 = gs->player.y + 13;
    LONG i;
    for (i = 0; i < gs->num_enemies; i++) {
        const Enemy *E = &gs->enemies[i];
        LONG ex1, ey1, ex2, ey2;
        if (!E->alive) continue;
        ex1 = E->x + 2; ey1 = E->y + 2;
        ex2 = E->x + 13; ey2 = E->y + 13;
        if (px1 <= ex2 && px2 >= ex1 && py1 <= ey2 && py2 >= ey1) return 1;
    }
    return 0;
}

static void kill_player(void)
{
    if (gs->lives > 0) gs->lives--;
    if (gs->lives <= 0) {
        gs->mode = GM_LOSE;
        gs->state_timer = 150;
    } else {
        game_restart_current();
    }
}

/* 4-way integer walk with tile collision. Resolve X then Y independently
 * so sliding along a wall feels right. */
static void update_player(UBYTE input)
{
    Player *p = &gs->player;
    LONG dx = 0, dy = 0;
    LONG nx, ny, cx, cy, col, row;

    if (input & INPUT_LEFT)  { dx = -WALK_SPEED; p->facing = 3; }
    if (input & INPUT_RIGHT) { dx = +WALK_SPEED; p->facing = 1; }
    if (input & INPUT_UP)    { dy = -WALK_SPEED; p->facing = 0; }
    if (input & INPUT_DOWN)  { dy = +WALK_SPEED; p->facing = 2; }

    /* X axis. */
    nx = p->x + dx;
    if (dx > 0) {
        if (wall_at(nx + 15, p->y + 1) || wall_at(nx + 15, p->y + 14))
            nx = ((p->x / TILE_W) + 1) * TILE_W - 16;
    } else if (dx < 0) {
        if (wall_at(nx, p->y + 1) || wall_at(nx, p->y + 14))
            nx = (p->x / TILE_W) * TILE_W;
    }
    p->x = nx;

    /* Y axis. */
    ny = p->y + dy;
    if (dy > 0) {
        if (wall_at(p->x + 1, ny + 15) || wall_at(p->x + 14, ny + 15))
            ny = ((p->y / TILE_H) + 1) * TILE_H - 16;
    } else if (dy < 0) {
        if (wall_at(p->x + 1, ny) || wall_at(p->x + 14, ny))
            ny = (p->y / TILE_H) * TILE_H;
    }
    p->y = ny;

    if ((dx | dy) && (gs->tick % 6) == 0) p->frame = (p->frame + 1) & 3;

    /* Pickup / interact at player centre. */
    cx = p->x + 8; cy = p->y + 8;
    col = cx / TILE_W; row = cy / TILE_H;
    if (col >= 0 && col < GRID_W && row >= 0 && row < GRID_H) {
        UBYTE *k = &gs->grid[row][col];
        if (*k == T_RUNE) {
            *k = T_FLOOR;
            gs->runes_remaining--;
            gs->score += RUNE_SCORE;
        } else if (*k == T_OIL) {
            *k = T_FLOOR;
            gs->lantern += OIL_REFILL;
            if (gs->lantern > LANTERN_MAX) gs->lantern = LANTERN_MAX;
            gs->score += OIL_SCORE;
        } else if (*k == T_HAZARD) {
            kill_player();
            return;
        } else if (*k == T_PORTAL && gs->runes_remaining == 0) {
            gs->score += LEVEL_SCORE + (gs->lantern / 2);
            gs->level_index++;
            if (gs->level_index >= NUM_LEVELS) {
                gs->mode = GM_WIN;
                gs->state_timer = 200;
            } else {
                gs->mode = GM_LEVELWIN;
                gs->state_timer = 60;
            }
            return;
        }
    }
}

static void update_enemies(void)
{
    LONG i;
    for (i = 0; i < gs->num_enemies; i++) {
        Enemy *E = &gs->enemies[i];
        if (!E->alive) continue;
        if (E->axis == 0) {
            E->x += E->dx;
            if (E->x <= (LONG)E->min_pos) { E->x = E->min_pos; E->dx = -E->dx; }
            if (E->x >= (LONG)E->max_pos) { E->x = E->max_pos; E->dx = -E->dx; }
        } else {
            E->y += E->dy;
            if (E->y <= (LONG)E->min_pos) { E->y = E->min_pos; E->dy = -E->dy; }
            if (E->y >= (LONG)E->max_pos) { E->y = E->max_pos; E->dy = -E->dy; }
        }
        E->frame++;
    }
}

static void drain_lantern(void)
{
    if ((gs->tick % LANTERN_DRAIN_FRAMES) == 0 && gs->lantern > 0)
        gs->lantern--;
    if (gs->lantern == 0) kill_player();
}

void game_tick(UBYTE input)
{
    gs->tick++;

    if (gs->mode == GM_TITLE) {
        if (input & INPUT_START) {
            gs->level_index = 0;
            gs->lives = 3;
            gs->score = 0;
            game_load_level(0);
            gs->mode = GM_PLAYING;
        }
        return;
    }
    if (gs->mode == GM_WIN || gs->mode == GM_LOSE) {
        if (gs->state_timer > 0) gs->state_timer--;
        if ((input & INPUT_START) && gs->state_timer == 0) gs->mode = GM_TITLE;
        return;
    }
    if (gs->mode == GM_LEVELWIN) {
        if (gs->state_timer > 0) { gs->state_timer--; return; }
        game_load_level(gs->level_index);
        gs->mode = GM_PLAYING;
        return;
    }

    /* GM_PLAYING */
    update_player(input);
    if (gs->mode != GM_PLAYING) return;
    update_enemies();
    drain_lantern();
    if (gs->mode != GM_PLAYING) return;
    if (touching_enemy()) kill_player();
}
