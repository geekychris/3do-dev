/*
 * main_3do.c - the amiga3d engine's 3DO main (the equivalent of main_68k.c
 * and main_os4.c), on the sdk/amiga layer: the 320 x 256 frame of 32 pens
 * shown as one cel, plat_sky's gradient as a cel under the frame (the
 * sky pen made transparent), the pad as the joystick and keys
 * (a3_pad_keys), ptplayer (the layer's) for the music and the effects.
 * 25 frames a second at most, against the display's field counter; the
 * games step their simulations by plat_ticks, so they keep their speed
 * when the frame rate drops.
 *
 * Built with -DA3D_TEST=1 (sdk/amiga3d/a3d.mk, A3D_TEST=1) it runs the
 * game's tests (run_tests: its core, including the long autopilot trace
 * hash of tests/vectors.h) and reports them as TDO:PASS / TDO:FAIL lines.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "graphics.h"
#include "celutils.h"
#include "amiga_internal.h"              /* after the 3DO headers: it undoes their AvailMem */
#include "bridge_client.h"
#include "ptplayer.h"
#include "tdo.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "a3d_3do.h"

#define CUSTOM_BASE ((void *)0)

int32_t plat_in_keys, plat_in_joy;
extern int32_t a3_stat_flush, a3_stat_cels, a3_stat_polys;
static Canvas canvas;
static const char *test_suite = "";

/* ---------- time: the display's field counter (60 Hz) ---------- */
static uint32_t fields(void)
{
    u32 f = 0;
    QueryGraphics(QUERYGRAF_TAG_FIELDCOUNT, &f);
    return (uint32_t)f;
}

int32_t plat_ticks(void)
{
    uint32_t f = fields();
    return (int32_t)((f / 6) * 5 + ((f % 6) * 5) / 6);
}

uint32_t plat_clock(void) { return fields(); }
int32_t plat_us_since(uint32_t t0) { return (int32_t)((fields() - t0) * 16667UL); }
void plat_activate(void) {}

/* ---------- the palette and the sky ---------- */
static uint16_t pal_now[32];
static int sky_pen = -1;
static CCB *sky_cel;
static uint16_t *sky_pix;               /* 4 x 256 15-bit pixels */

void plat_palette(const uint16_t pal[32])
{
    memcpy(pal_now, pal, sizeof(pal_now));
    gfx_load_rgb4((const UWORD *)pal, 32);
}

/* 12-bit RGB to the 3DO's 15-bit, never 0 (an uncoded cel's transparent value) */
static uint16_t rgb15(int r4x, int g4x, int b4x)    /* each 0..15 * 16 */
{
    int r = (r4x * 31 + 120) / 240, g = (g4x * 31 + 120) / 240, b = (b4x * 31 + 120) / 240;
    uint16_t v = (uint16_t)((r << 10) | (g << 5) | b);
    return v ? v : 1;
}

void plat_sky(int pen, uint16_t top, uint16_t bottom, int rows)
{
    int y, x;
    if (rows <= 0 || pen < 0 || pen > 31) {
        gfx_set_transparent_pen(-1);
        gfx_set_underlay(0);
        sky_pen = -1;
        a3_set_sky(-1, 0, 0);
        return;
    }
    if (!sky_pix) {
        sky_pix = (uint16_t *)malloc(4 * SCREEN_H * 2);
        if (!sky_pix) return;
        sky_cel = CreateCel(4, SCREEN_H, 16, CREATECEL_UNCODED, sky_pix);
        if (!sky_cel) return;
        sky_cel->ccb_Flags |= CCB_BGND | CCB_LAST;
        sky_cel->ccb_XPos = 0;
        sky_cel->ccb_YPos = 0;
        sky_cel->ccb_HDX = 80 << 20;    /* 4 pixels across the screen */
        sky_cel->ccb_HDY = 0;
        sky_cel->ccb_VDX = 0;
        sky_cel->ccb_VDY = (240 << 16) / SCREEN_H;
    }
    if (rows > SCREEN_H) rows = SCREEN_H;
    for (y = 0; y < SCREEN_H; y++) {
        int k = y < rows ? y : rows - 1, d = rows > 1 ? rows - 1 : 1;
        int r = ((top >> 8) & 15) * 16 + ((((bottom >> 8) & 15) - ((top >> 8) & 15)) * 16 * k) / d;
        int g = ((top >> 4) & 15) * 16 + ((((bottom >> 4) & 15) - ((top >> 4) & 15)) * 16 * k) / d;
        int b = (top & 15) * 16 + (((bottom & 15) - (top & 15)) * 16 * k) / d;
        uint16_t c = rgb15(r, g, b);
        for (x = 0; x < 4; x++) sky_pix[y * 4 + x] = c;
    }
    sky_pen = pen;
    a3_set_sky(pen, sky_pix, 4);
    gfx_set_transparent_pen(pen);
    gfx_set_underlay(sky_cel);
}

/* ---------- sound: the layer's ptplayer ---------- */
void *a3_sound_alloc(int len) { return AllocMem((ULONG)len, MEMF_CHIP | MEMF_CLEAR); }
void a3_sound_free(void *p, int len) { FreeMem(p, (ULONG)len); }

void a3_sound_play(int8_t *data, int len, int period, int vol, int pri)
{
    static SfxStructure s[4];
    static int k;
    SfxStructure *x = &s[k++ & 3];
    x->sfx_ptr = data;
    x->sfx_len = (WORD)(len / 2);
    x->sfx_per = (WORD)period;
    x->sfx_vol = (WORD)vol;
    x->sfx_cha = -1;
    x->sfx_pri = (BYTE)(pri ? pri : 1);
    mt_playfx(CUSTOM_BASE, x);
}

/* ---------- the bridge's test harness: to the debug console ---------- */
static int t_pass, t_fail;

void ab_test_begin(const char *suite) { test_suite = suite; t_pass = t_fail = 0; }
int ab_test_assert(int cond, const char *name, const char *file, int line)
{
    (void)file; (void)line;
    if (cond) t_pass++;
    else { t_fail++; tdo_log("A3D: %s FAIL %s\n", test_suite, name); }
    return cond;
}
void ab_test_end(void) { tdo_log("A3D: %s %d passed, %d failed\n", test_suite, t_pass, t_fail); }

int run_tests(void (*check)(int cond, const char *name));

#ifdef A3D_TEST
/* ---------- the test build: the game's tests on the 3DO ---------- */
static void check(int cond, const char *name) { tdo_check(name, cond); }

int main(void)
{
    uint32_t f0;
    char line[64];
    ab_init(game_client);
    gfx_init(0, 0);
    gfx_set_rgb4(0, 0x000);
    gfx_set_rgb4(1, 0xFFF);
    a3_draw_init();
    canvas.pix = gfx_pixels8();
    canvas.w = SCREEN_W;
    canvas.h = SCREEN_H;
    canvas.bpp = 1;
    g_text(&canvas, 8, 20, 1, "RUNNING THE TESTS...");
    gfx_swap();
    gfx_swap();
    tdo_log("A3D: tests start %s\n", game_client);
    f0 = fields();
    run_tests(check);
    sprintf(line, "TESTS DONE IN %ld S", (long)((fields() - f0) / 60));
    tdo_log("A3D: tests took %d fields\n", (int)(fields() - f0));
    g_text(&canvas, 8, 40, 1, line);
    tdo_test_done();
    for (;;) gfx_swap();
    return 0;
}

#else
/* ---------- the game ---------- */
static unsigned pad_bits(uint32_t held, unsigned *joy_out)
{
    unsigned joy = 0, in = 0;
    const A3PadKey *k;
    if (held & PAD_LEFT) joy |= A3_JOY_LEFT;
    if (held & PAD_RIGHT) joy |= A3_JOY_RIGHT;
    if (held & PAD_UP) joy |= A3_JOY_UP;
    if (held & PAD_DOWN) joy |= A3_JOY_DOWN;
    for (k = a3_pad_keys; k->pad; k++)
        if (held & k->pad) {
            joy |= k->joy;
            if (k->key) in |= game_key_bit(k->key);
        }
    *joy_out = joy;
    return in;
}

int main(void)
{
    LONG mod_len = 0;
    UBYTE *mod = 0;
    uint32_t base, now, fps_f0;
    int32_t n = 0, fps_frames = 0;
    uint32_t st_f0 = 0, t0, t1, t2, t3, f_upd = 0, f_draw = 0, f_wait = 0, f_swap = 0;
    int running = 1;

    ab_init(game_client);
    amiga_set_progdir(game_client);
    a3_draw_init();
    if (!gfx_init(0, 0)) return 1;
    canvas.pix = gfx_pixels8();
    canvas.w = SCREEN_W;
    canvas.h = SCREEN_H;
    canvas.bpp = 1;
    game_init();

    mt_install_cia(CUSTOM_BASE, 0, 1);
    mt_musicmask(CUSTOM_BASE, 0x03);    /* effects never take the melody or bass */
    if (game_music[0]) mod = (UBYTE *)amiga_load_file((CONST_STRPTR)game_music, &mod_len);
    if (mod) {
        mt_init(CUSTOM_BASE, mod, 0, 0);
        mt_MusicChannels = 2;
        mt_Enable = 1;
        AB_I("music loaded (%ld bytes)", (long)mod_len);
    } else if (game_music[0]) AB_W("%s not found - no music", game_music);
    game_sound_init();
    if (game_voice[0]) AB_I("voice: %ld clips", (long)a3_voice_load(game_voice));
    tdo_log("A3D: ready %s\n", game_client);

    base = fps_f0 = fields();
    while (running) {
        unsigned joy, in;
        uint32_t held = pad_held(0) | pad_pressed(0), due;
        static int window10;

        if (amiga_quit_requested()) break;
        in = pad_bits(held, &joy);
        if (++window10 > 250) { window10 = 0; plat_in_keys = plat_in_joy = 0; }
        plat_in_joy |= (int32_t)joy;
        plat_in_keys |= (int32_t)in;
        in |= game_joy_bits(joy);
        if (!game_input(in)) running = 0;
        t0 = fields();
        game_update(in);
        t1 = fields();
        a3_frame_begin(&canvas);
        game_draw(&canvas, 0);
        a3_frame_end();
        game_after_frame();
        t2 = fields();

        /* 25 frames a second at most: frame n is due at field n * 12 / 5 */
        n++;
        due = base + (uint32_t)((n * 12) / 5);
        now = fields();
        if ((int32_t)(due - now) > 0) amiga_wait_vbls((int)(due - now));
        else if ((int32_t)(now - due) > 6) { base = now; n = 0; }   /* slow: re-sync */
        t3 = fields();
        gfx_swap();
        f_upd += t1 - t0; f_draw += t2 - t1; f_wait += t3 - t2; f_swap += fields() - t3;

        fps_frames++;
        now = fields();
        if (now - st_f0 >= 60) {
            static char st[160], last[160];
            st_f0 = now;
            st[0] = 0;
            a3_game_status(st);
            if (strcmp(st, last)) { tdo_log("A3D: st %s\n", st); strcpy(last, st); }
        }
        if (now - fps_f0 >= 300) {      /* every 5 s */
            int32_t t10 = (fps_frames * 600L) / (int32_t)(now - fps_f0);
            tdo_log("A3D: fps=%d.%d frames=%d fields: update=%d draw=%d wait=%d swap=%d; a frame: polys=%d cels=%d\n", (int)(t10 / 10), (int)(t10 % 10),
                    (int)fps_frames, (int)f_upd, (int)f_draw, (int)f_wait, (int)f_swap,
                    (int)(a3_stat_polys / fps_frames), (int)(a3_stat_cels / fps_frames));
            a3_stat_polys = a3_stat_cels = 0;
            f_upd = f_draw = f_wait = f_swap = 0;
            fps_frames = 0;
            fps_f0 = now;
        }
    }
    AB_I("%s shutting down", game_title);
    mt_end(CUSTOM_BASE);
    mt_remove_cia(CUSTOM_BASE);
    a3_sfx_cleanup();
    if (mod) FreeVec(mod);
    if (sky_cel) { gfx_set_underlay(0); DeleteCel(sky_cel); }
    if (sky_pix) free(sky_pix);
    gfx_exit();                         /* logs AMIGA3DO: exit */
    return 0;
}
#endif
