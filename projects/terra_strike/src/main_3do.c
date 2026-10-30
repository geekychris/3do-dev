/*
 * main_3do.c - Terra Strike on the 3DO: the platform side that main.c is
 * on the classic Amiga and main_os4.c on AmigaOS 4, on sdk/amiga.
 *
 * The frame is the layer's 320 x 256 buffer of pens (render.c's AmigaOS 4
 * path draws into it); the 3D is filled by the cel engine in a layer under
 * it (cels_3do.c), through the shadow pen made transparent. The music is
 * terra_strike.mod on the layer's ptplayer, the effects sound.c's. 25
 * frames a second at most against the display's field counter; the game
 * steps its simulation by plat_ticks, so it keeps its speed when slower.
 *
 *   D-pad   turn, climb / dive        A   cannon (and start)
 *   B / C   throttle up / down        P   start (title), pause (play)
 *   L       back to the title         X   quit to the menu
 *
 * Built with -DTS_TEST=1 (make TS_TEST=1 BUILD=build/selftest) it runs the
 * game's unit tests and the scripted flight (tests/trace.c) instead.
 */
#include <stdio.h>
#include <string.h>
#include "graphics.h"
#include "amiga_internal.h"             /* after the 3DO headers: it undoes their AvailMem */
#include "bridge_client.h"
#include "ptplayer.h"
#include "tdo.h"
#include "ts.h"
#include "models.h"
#include "render.h"
#include "title_image.h"
#include "sound.h"
#include "game.h"
#include "cels_3do.h"

static const UWORD palette[32] = {     /* main.c's */
    0x000, 0xFFF, 0x333, 0x999,
    0x027, 0x04A,
    0xB95, 0xDB7,
    0x392, 0x5B3,
    0x161, 0x283,
    0x654, 0x987,
    0xAAB, 0xEEF,
    0xF30, 0x900, 0xFE0, 0xF80,
    0x9EF, 0x59B, 0x357,
    0xC64, 0x732,
    0xDDC, 0x998, 0xB43,
    0xABC, 0x567,
    0x2C3, 0x111
};

static Canvas canvas;

static uint32_t fields(void)
{
    u32 f = 0;
    QueryGraphics(QUERYGRAF_TAG_FIELDCOUNT, &f);
    return (uint32_t)f;
}

/* ---------- what game.c needs from the platform ---------- */
int32_t plat_ticks(void)
{
    uint32_t f = fields();
    return (int32_t)((f / 6) * 5 + ((f % 6) * 5) / 6);
}
uint32_t plat_clock(void) { return fields(); }
int32_t plat_us_since(uint32_t t0) { return (int32_t)((fields() - t0) * 16667UL); }

void plat_palette(int title)
{
    gfx_load_rgb4(title ? (const UWORD *)title_palette : palette, 32);
    /* the 3D shows through the shadow pen; the artwork uses all 32 */
    gfx_set_transparent_pen(title ? -1 : PEN_SHADOW);
    if (title) gfx_set_underlay(0);
}

/* ---------- the bridge's test harness: to the debug console ---------- */
void ab_test_begin(const char *suite) { (void)suite; }
int ab_test_assert(int cond, const char *name, const char *file, int line)
{
    (void)file; (void)line;
    tdo_check(name, cond);
    return cond;
}
void ab_test_end(void) {}

#ifdef TS_TEST
void ts_trace(void (*out)(const char *line));
static void check(int cond, const char *name) { tdo_check(name, cond); }
static void trace_line(const char *s) { tdo_log("TRACE %s\n", s); }

int main(void)
{
    uint32_t f0;
    ab_init("terra_strike");
    gfx_init(palette, 32);
    tdo_log("TS: tests start\n");
    f0 = fields();
    ts_run_tests(check);
    ts_trace(trace_line);
    tdo_log("TS: tests took %d fields\n", (int)(fields() - f0));
    tdo_test_done();
    for (;;) gfx_swap();
    return 0;
}

#else
/* the pad as the game's input bits */
static unsigned pad_in(uint32_t p)
{
    unsigned in = 0;
    if (p & PAD_LEFT) in |= IN_LEFT;
    if (p & PAD_RIGHT) in |= IN_RIGHT;
    if (p & PAD_UP) in |= IN_UP;
    if (p & PAD_DOWN) in |= IN_DOWN;
    if (p & PAD_A) in |= IN_FIRE;
    if (p & PAD_B) in |= IN_FAST;
    if (p & PAD_C) in |= IN_SLOW;
    if (p & PAD_P) in |= (world.state == GS_PLAY || world.state == GS_LEVELCLEAR) ? IN_PAUSE : IN_START;
    if (p & PAD_L) in |= IN_ESC;
    return in;
}

int main(void)
{
    LONG mod_len = 0;
    UBYTE *mod;
    uint32_t base, now, fps_f0, st_f0;
    int32_t n = 0, fps_frames = 0;
    int running = 1;

    ab_init("terra_strike");
    amiga_set_progdir("terra_strike");
    if (!gfx_init(palette, 32)) return 1;   /* first: plat_ticks needs the graphics folio */
    game_init();
    if (!render_init()) return 1;
    canvas.pix = gfx_pixels8();
    gfx_set_transparent_pen(PEN_SHADOW);

    sound_init();
    mod = (UBYTE *)amiga_load_file((CONST_STRPTR)"PROGDIR:terra_strike.mod", &mod_len);
    mt_install_cia(CUSTOM_BASE, 0, 1);
    if (mod) {
        mt_init(CUSTOM_BASE, mod, 0, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        AB_I("music loaded (%ld bytes)", (long)mod_len);
    } else AB_W("terra_strike.mod not found - no music");
    AB_I("island seed %ld, pad at tile %ld,%ld height %ld, %ld targets",
         (long)world.seed, (long)world.pad_tx, (long)world.pad_tz, (long)world.pad_h, (long)world.targets_left);
    tdo_log("TS: ready\n");

    base = fps_f0 = st_f0 = fields();
    while (running) {
        unsigned in;
        uint32_t due;
        if (amiga_quit_requested()) break;
        in = pad_in(pad_held(0) | pad_pressed(0));
        if (!game_input(in)) running = 0;
        game_update(in);
        game_draw(&canvas, 0);          /* one pen buffer: the HUD cache's buffer 0 */
        game_after_frame();

        n++;                            /* 25 fps at most: frame n is due at field n * 12 / 5 */
        due = base + (uint32_t)((n * 12) / 5);
        now = fields();
        if ((int32_t)(due - now) > 0) amiga_wait_vbls((int)(due - now));
        else if ((int32_t)(now - due) > 6) { base = now; n = 0; }
        gfx_swap();

        fps_frames++;
        now = fields();
        if (now - st_f0 >= 60) {        /* the state once a second, for the tests */
            static char last[160];
            char st[160];
            int k, shots = 0;
            st_f0 = now;
            for (k = 0; k < MAX_SHOTS; k++) shots += world.shots[k].active != 0;
            sprintf(st, "state=%ld pstate=%ld level=%ld score=%ld lives=%ld targets=%ld fuel=%ld speed=%ld yaw=%ld y=%ld shots=%ld paused=%ld",
                    (long)world.state, (long)world.p.state, (long)world.level, (long)world.score, (long)world.lives,
                    (long)world.targets_left, (long)world.p.fuel, (long)world.p.speed, (long)world.p.yaw,
                    (long)(world.p.y >> FBITS), (long)shots, (long)paused);
            if (strcmp(st, last)) { tdo_log("TS: st %s\n", st); strcpy(last, st); }
        }
        if (now - fps_f0 >= 300) {
            int32_t t10 = (fps_frames * 600L) / (int32_t)(now - fps_f0);
            tdo_log("TS: fps=%d.%d polys=%d cels=%d\n", (int)(t10 / 10), (int)(t10 % 10),
                    (int)render_stats.polys, (int)ts3_ncels);
            fps_frames = 0;
            fps_f0 = now;
        }
    }
    AB_I("Terra Strike shutting down, score %ld", (long)world.score);
    mt_end(CUSTOM_BASE);
    mt_remove_cia(CUSTOM_BASE);
    if (mod) FreeVec(mod);
    sound_cleanup();
    render_cleanup();
    gfx_set_underlay(0);
    gfx_exit();                         /* logs AMIGA3DO: exit */
    return 0;
}
#endif
