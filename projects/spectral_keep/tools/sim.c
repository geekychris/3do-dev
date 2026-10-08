/*
 * Host-side checks of the game logic (world.c, room.c, game.c and the real
 * levels), no emulator needed:
 *
 *   cc -std=c89 -Isrc -o build/sim tools/sim.c src/world.c src/room.c src/game.c src/levels.c && build/sim
 *
 * Each scenario puts the player somewhere in a shipped room, steps the
 * logic at 50 steps/s and checks what the Unity game would do.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "keep.h"

static char last_log[64][120];
static int nlog, fails;

void keep_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsprintf(last_log[nlog++ & 63], fmt, ap);
    va_end(ap);
}
int  snd_init(void) { return 1; }
void snd_play(int s) { (void)s; }
void snd_music(int t) { (void)t; }
void snd_duck(long t) { (void)t; }
void snd_music_enable(int on) { (void)on; }

static int logged(const char *s)
{
    int i;
    for (i = 0; i < 64; i++)
        if (strstr(last_log[i], s)) return 1;
    return 0;
}

static void check(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

#define U(v) ((fix)((v) * 4096))
#define PAD_UP 1
#define PAD_DOWN 2
#define PAD_LEFT 4
#define PAD_RIGHT 8
#define PAD_A 16

static void clear_log(void) { memset(last_log, 0, sizeof(last_log)); }
static void steps(int n, unsigned long held) { while (n-- > 0) game_step(held, 0); }

static int find_room(const char *id)
{
    int i;
    for (i = 0; i < G.level->nrooms; i++)
        if (!strcmp(G.level->rooms[i].id, id)) return i;
    return -1;
}

/* start keep `lv` and put the player at (x, y, z) in room `id` */
static void place(int lv, const char *id, double x, double y, double z)
{
    game_init();
    G.selected = lv;
    game_step(0, PAD_A);                         /* title -> playing */
    room_build(G.level, find_room(id), 1);
    room_spawn_player(U(x), U(y), U(z));
    G.entry_x = U(x); G.entry_y = U(y); G.entry_z = U(z);
    clear_log();
}

static Body *player(void) { return R.act[R.player].body; }

int main(void)
{
    int i;
    /* grid controls: the pad's directions are world axes */
    printf("lifts\n");
    place(0, "armoury", 0, 0, 0);
    for (i = 0; i < R.nlift; i++) {
        Body *l = R.lift[i].body;
        player()->px = l->px; player()->pz = l->pz; player()->py = l->py + U(0.25);
    }
    {
        fix top = 0;
        for (i = 0; i < 200; i++) { steps(1, 0); if (player()->py > top) top = player()->py; }
        check("a lift carries the player up two blocks", top > U(2.1));
        for (i = 0; i < 150 && player()->py > U(0.3); i++) steps(1, 0);
        check("and back down (on the platform, 0.25 up)", player()->py <= U(0.3));
    }

    printf("crates\n");
    place(0, "armoury", 0, 0, 0);
    for (i = 0; i < R.nact; i++)
        if (R.act[i].kind == K_CRATE) break;
    {
        Body *c = R.act[i].body;
        fix cx;
        G.grid_controls = 1;
        c->px = U(3.5); c->pz = U(4.5);          /* out of its corner, into the open */
        cx = c->px;
        player()->px = c->px - U(1.0); player()->pz = c->pz; player()->py = c->py;
        steps(40, PAD_RIGHT);
        check("walking into a crate pushes it", c->px > cx + U(0.3));
    }

    printf("gates and keys\n");
    place(0, "throne", 0, 0, 0);
    check("the throne room has its gates", R.ngate == 2);
    G.grid_controls = 1;
    {
        /* the gates fill the west doorway, the only way in: walk at them from inside */
        player()->px = U(1.6); player()->pz = U(3.6); player()->py = 0;
        G.keys = 0;
        steps(60, PAD_LEFT);
        check("a gate stays shut without a key", R.ngate == 2 && strstr(G.message, "LOCKED") != 0);
        G.keys = 1;
        steps(60, PAD_LEFT);
        check("a key opens the gates", R.ngate == 0 && G.keys == 0 && logged("gate throne"));
    }

    printf("the throne\n");
    place(0, "throne", 0, 0, 0);
    G.relics = 0;
    player()->px = U(R.tx + 0.5); player()->pz = U(R.tz - 0.6); player()->py = U(R.ty);
    G.grid_controls = 1;
    steps(30, PAD_UP);
    check("without the relics the throne asks for more", G.state == GS_PLAYING && strstr(G.message, "MORE RELIC") != 0);
    G.relics = G.level->relics;
    G.last_nag = -1000;
    steps(30, PAD_UP);
    check("with them the keep is done", G.state == GS_LEVELDONE);
    steps(200, 0);
    check("then the next keep starts", G.state == GS_PLAYING && G.level == &keep_levels[1] && G.lives > 0);

    printf("enemies\n");
    place(0, "hall", 4.5, 0, 0.6);
    G.grid_controls = 1;
    steps(800, 0);
    check("the ghost drifts through blocks and kills", logged("death A GHOST"));
    place(0, "well", 0.5, 0, 4.5);
    steps(30, 0);
    for (i = 0; i < R.nact; i++)
        if (R.act[i].kind == K_HOUND) break;
    check("the hound sleeps while you're away", i < R.nact && !R.act[i].awake);
    player()->px = R.act[i].body->px - U(3); player()->pz = R.act[i].body->pz;
    steps(2, 0);
    check("and wakes when you come near", R.act[i].awake);
    place(0, "throne", 0, 0, 0);
    for (i = 0; i < R.nact; i++)
        if (R.act[i].kind == K_BOUNCER) break;
    {
        fix x0 = R.act[i].body->px, z0 = R.act[i].body->pz, maxy = 0;
        int k;
        player()->px = U(0.5); player()->pz = U(0.5);
        R.act[R.player].invuln = 100000;
        for (k = 0; k < 300; k++) {
            steps(1, 0);
            if (R.act[i].body->py > maxy) maxy = R.act[i].body->py;
        }
        check("the bouncer bounces and travels", maxy > U(0.5) &&
              (R.act[i].body->px != x0 || R.act[i].body->pz != z0));
    }

    printf("spikes and lives\n");
    place(0, "well", 0.5, 0, 6.5);
    G.grid_controls = 1;
    steps(60, PAD_RIGHT);
    check("spikes kill", logged("death SPIKES") && G.lives == 3);
    steps(100, 0);
    check("respawn where you came in", G.state == GS_PLAYING && player()->px < U(1));
    G.lives = 0;
    R.act[R.player].invuln = 0;
    steps(60, PAD_RIGHT);
    steps(100, 0);
    check("no lives left: game over", G.state == GS_GAMEOVER);

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
