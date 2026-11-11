/*
 * The original's rules on the host, for tools/check_levels.py: player.c and
 * level.c exactly as the game builds them (tools/hostinc: the Amiga types),
 * driven frame by frame as main.c does (the player, the bricks growing back,
 * the hidden ladders once all the gold is in; won with all the gold on row
 * 0). The guards are left out: the check is whether a level's layout can
 * be finished.
 *
 *   (tools/check_levels.py builds it: tools/build/hostcheck.so)
 */
#include <string.h>
#include "game.h"
#include "level.h"
#include "player.h"

GameState gs;

void enemy_trap_check(GameState *g, int x, int y) { (void)g; (void)x; (void)y; }

int hc_size(void) { return sizeof(GameState); }

/* a level as the game loads it (level.c's data) */
void hc_load(int level)
{
    level_load(&gs, level);
    gs.num_enemies = 0;
    memset(gs.enemies, 0, sizeof(gs.enemies));
}

/* a level from tiles (column-major [x][y], with the player marker) */
void hc_custom(const UBYTE *tiles)
{
    memcpy(gs.tiles, tiles, sizeof(gs.tiles));
    memset(gs.brick_timer, 0, sizeof(gs.brick_timer));
    level_activate_spawn_markers(&gs);
    gs.num_enemies = 0;
    memset(gs.enemies, 0, sizeof(gs.enemies));
    gs.gold_total = level_count_gold();
    gs.gold_collected = 0;
}

/* one frame: 0 on, 1 won, 2 dead */
int hc_tick(int dx, int dy)
{
    player_update(&gs, dx, dy, 0, 0);
    level_tick_bricks(&gs);
    if (gs.gold_collected >= gs.gold_total) {
        level_reveal_hidden_ladders(&gs);
        if (gs.player.gy == 0) return 1;            /* (main.c: all the gold and row 0) */
    }
    if (gs.player.state == PS_DEAD) return 2;
    return 0;
}

int hc_gx(void) { return gs.player.gx; }
int hc_gy(void) { return gs.player.gy; }
int hc_aligned(void) { return (gs.player.px % TILE_W) == 0 && (gs.player.py % TILE_H) == 0; }
int hc_state(void) { return gs.player.state; }
int hc_gold(void) { return gs.gold_collected; }
int hc_gold_total(void) { return gs.gold_total; }
