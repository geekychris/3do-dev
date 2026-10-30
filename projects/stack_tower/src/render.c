// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Stack Tower's picture on the shared engine: the tower in 3D
 * (the core, every block as a brick sticking out of it, the falling piece
 * and its ghost, the red line), the panel with the score, the next pieces,
 * the held one and the unrolled map of the whole tower, and the title,
 * help, pause and game-over screens.
 *
 * The tower turns so the falling piece's column faces you; blocks on the
 * back half are hidden by the core, so only the front half is drawn.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "a3d_i64.h"
#include "st.h"
#include "stack.h"

/* palette: 0 black, 1 white, 2 panel, 3 grey, 4 sky (gradient), 5-6 the
 * core, 7 the red line, 8..23 the pieces (lit, shaded), 24-25 garbage,
 * 26 ghost, 27 yellow, 28 cyan, 29 green, 30 orange, 31 dark */
const uint16_t stack_palette[32] = {
    0x000, 0xFFF, 0x223, 0x99A, 0x113, 0x334, 0x557, 0xE22,
    0xE44, 0x922, 0xF92, 0xA51, 0xFE4, 0xA92, 0x5D4, 0x283,
    0x4CE, 0x278, 0x46E, 0x239, 0xB5E, 0x628, 0xE6A, 0x935,
    0x888, 0x555, 0x446, 0xFE2, 0x2EF, 0x3F5, 0xF82, 0x111
};
#define PEN_SKY     4
#define PEN_CORE    5
#define PEN_LINE    7
#define PEN_PIECE   8
#define PEN_GARBAGE 24
#define PEN_GHOST   26
#define PEN_YELLOW  27
#define PEN_CYAN    28
#define PEN_GREEN   29
#define PEN_ORANGE  30
#define PEN_CORE_LINE 2                 /* the grid on the core (the panel's colour) */
#define PEN_GUIDE   6                   /* the falling piece's columns, down to where it lands */

#define TOWER_R   140                   /* the core's radius */
#define BRICK     22                    /* how far a block sticks out */
#define ROW_H     26
#define VIEW_H    256

static Cam cam;

static int lit_pen(int v)   { return v == ST_GARBAGE ? PEN_GARBAGE : PEN_PIECE + 2 * (v - 1); }
static int shade_pen(int v) { return lit_pen(v) + 1; }

/* the point at column position colf (8.8 fixed, relative to the column
 * facing you), radius r, row height y */
static void tower_pt(int colf, int r, int32_t y, V3 *out)
{
    int a = (colf * 4096) >> 8;
    cam_xform(&cam, (a3_sin(a) * r) >> 16, y, -((a3_cos(a) * r) >> 16), out);
}

/* a quad, drawn only if it faces the camera: corners clockwise as seen
 * from the side it faces */
static void face(V3 a, V3 b, V3 c, V3 d, int col)
{
    int32_t ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    int32_t vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    if (a3_dot3_nonneg(nx, a.x, ny, a.y, nz, a.z)) return;    /* 3DO: no int64_t */
    a3_quad(&cam, (a.z + b.z + c.z + d.z) >> 2, col, a, b, c, d);
}

/* which neighbours a block has (their shared faces aren't drawn) */
#define NB_LEFT  1
#define NB_RIGHT 2
#define NB_UP    4
#define NB_DOWN  8

/* one block: its outside face bevelled (a shaded rim round a lit centre,
 * so every cell reads on its own), its top, and the sides that show.
 * Blocks touch with no gap; faces against a neighbour are left out. */
static void brick(int colf, int row, int lit, int shade, int out, int nb)
{
    V3 p[8], q[4];
    int32_t y0 = (int32_t)row * ROW_H, y1 = y0 + ROW_H;
    int c0 = colf - 128, c1 = colf + 128, r0 = TOWER_R, r1 = TOWER_R + out;
    tower_pt(c0, r0, y0, &p[0]); tower_pt(c1, r0, y0, &p[1]);
    tower_pt(c1, r0, y1, &p[2]); tower_pt(c0, r0, y1, &p[3]);
    tower_pt(c0, r1, y0, &p[4]); tower_pt(c1, r1, y0, &p[5]);
    tower_pt(c1, r1, y1, &p[6]); tower_pt(c0, r1, y1, &p[7]);
    face(p[4], p[7], p[6], p[5], shade);            /* outside: the rim */
    if (lit != shade) {                             /* and its lit centre, just in front */
        tower_pt(c0 + 44, r1 + 1, y0 + 5, &q[0]); tower_pt(c1 - 44, r1 + 1, y0 + 5, &q[1]);
        tower_pt(c1 - 44, r1 + 1, y1 - 5, &q[2]); tower_pt(c0 + 44, r1 + 1, y1 - 5, &q[3]);
        {
            int32_t ux = q[3].x - q[0].x, uy = q[3].y - q[0].y, uz = q[3].z - q[0].z;
            int32_t vx = q[2].x - q[0].x, vy = q[2].y - q[0].y, vz = q[2].z - q[0].z;
            int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            if (!a3_dot3_nonneg(nx, q[0].x, ny, q[0].y, nz, q[0].z))
                a3_quad(&cam, ((p[4].z + p[5].z + p[6].z + p[7].z) >> 2) - 6, lit, q[0], q[3], q[2], q[1]);
        }
    }
    if (!(nb & NB_UP)) face(p[7], p[3], p[2], p[6], lit);       /* top */
    if (!(nb & NB_DOWN)) face(p[4], p[5], p[1], p[0], shade);   /* bottom */
    if (!(nb & NB_LEFT)) face(p[0], p[3], p[7], p[4], shade);   /* left side */
    if (!(nb & NB_RIGHT)) face(p[1], p[5], p[6], p[2], shade);  /* right side */
}

static int occupied(const StGame *g, int c, int r)
{
    if (r < 0 || r >= ST_TOTAL) return r < 0;       /* the floor counts as a neighbour below */
    return g->board[r * ST_COLS + ((c + ST_COLS) % ST_COLS)] != 0;
}

static int board_nb(const StGame *g, int c, int r)
{
    return (occupied(g, c - 1, r) ? NB_LEFT : 0) | (occupied(g, c + 1, r) ? NB_RIGHT : 0) |
           (occupied(g, c, r + 1) ? NB_UP : 0) | (occupied(g, c, r - 1) ? NB_DOWN : 0);
}

/* a piece's own neighbours among its cells */
static int piece_nb(int cells[][2], int n, int k)
{
    int j, nb = 0;
    for (j = 0; j < n; j++) {
        int dc = ((cells[j][0] - cells[k][0] + ST_COLS + ST_COLS / 2) % ST_COLS) - ST_COLS / 2, dr = cells[j][1] - cells[k][1];
        if (dr == 0 && dc == -1) nb |= NB_LEFT;
        if (dr == 0 && dc == 1) nb |= NB_RIGHT;
        if (dc == 0 && dr == 1) nb |= NB_UP;
        if (dc == 0 && dr == -1) nb |= NB_DOWN;
    }
    return nb;
}

static void ring_band(int32_t y0, int32_t y1, int col, int r)
{
    int k;
    for (k = -8; k < 8; k++) {
        V3 a, b, c, d;
        int c0 = k * 256 - (stack.view & 255), c1 = c0 + 256;
        tower_pt(c0, r, y0, &a); tower_pt(c1, r, y0, &b);
        tower_pt(c1, r, y1, &c); tower_pt(c0, r, y1, &d);
        face(a, d, c, b, col);
    }
}

static void draw_tower(Canvas *cv, const StGame *g)
{
    int c, r, k, cells[5][2], n, ghost;
    int vcol = stack.view;              /* the facing column, 8.8 */

    cam.x = 0;
    cam.y = 300;
    cam.z = -640;
    cam.yaw = 0;
    cam.pitch = 900;
    cam_view(&cam, 200, VIEW_H, 100, 120, 200);
    cam_setup(&cam);
    a3_begin(1600);

    /* the core: one grey, 16 panels whose edges are the columns' edges */
    for (k = 0; k < 16; k++) {
        V3 a, b, cc, d;
        int c0 = (k << 8) - 128 - (vcol & 255), c1 = c0 + 256;
        tower_pt(c0, TOWER_R - 3, -20, &a); tower_pt(c1, TOWER_R - 3, -20, &b);
        tower_pt(c1, TOWER_R - 3, ST_ROWS * ROW_H + 30, &cc); tower_pt(c0, TOWER_R - 3, ST_ROWS * ROW_H + 30, &d);
        face(a, d, cc, b, PEN_CORE);
    }
    /* the core is behind everything else: drawn now, in passes (its tall
     * panels would otherwise sort in front of what lies low on them) */
    a3_flush(cv);
    a3_begin(1600);
    /* the drop guide: the falling piece's columns, from the piece down to
     * where it will land */
    if (!g->over && stack.state == SS_PLAY) {
        Piece gp = g->piece;
        int top[ST_COLS], bot[ST_COLS];
        for (c = 0; c < ST_COLS; c++) { top[c] = -1; bot[c] = 1 << 20; }
        gp.row = st_ghost_row(g);
        n = st_cells_of(&g->piece, cells);
        for (k = 0; k < n; k++) if (cells[k][1] > top[cells[k][0]]) top[cells[k][0]] = cells[k][1];
        n = st_cells_of(&gp, cells);
        for (k = 0; k < n; k++) if (cells[k][1] < bot[cells[k][0]]) bot[cells[k][0]] = cells[k][1];
        for (c = 0; c < ST_COLS; c++) {
            V3 a, b, cc, d;
            int rel;
            if (top[c] < 0 || bot[c] > top[c]) continue;
            rel = (((c << 8) - vcol + 8 * 256) & (16 * 256 - 1)) - 8 * 256;
            for (r = bot[c]; r <= top[c]; r++) {         /* a quad a row, so each sorts with the blocks beside it */
                tower_pt(rel - 128, TOWER_R - 2, (int32_t)r * ROW_H, &a); tower_pt(rel + 128, TOWER_R - 2, (int32_t)r * ROW_H, &b);
                tower_pt(rel + 128, TOWER_R - 2, (int32_t)(r + 1) * ROW_H, &cc); tower_pt(rel - 128, TOWER_R - 2, (int32_t)(r + 1) * ROW_H, &d);
                face(a, d, cc, b, PEN_GUIDE);
            }
        }
    }
    /* the grid: a thin line at every column's and row's edge on the front
     * half, where no block hides it */
    for (k = -5; k <= 6; k++) {
        V3 a, b, cc, d;
        int ce = (k << 8) - 128 - (vcol & 255);
        tower_pt(ce - 6, TOWER_R - 1, 0, &a); tower_pt(ce + 6, TOWER_R - 1, 0, &b);
        tower_pt(ce + 6, TOWER_R - 1, (int32_t)ST_ROWS * ROW_H, &cc); tower_pt(ce - 6, TOWER_R - 1, (int32_t)ST_ROWS * ROW_H, &d);
        face(a, d, cc, b, PEN_CORE_LINE);
    }
    for (r = 0; r <= ST_ROWS; r++)
        for (k = -5; k <= 5; k++) {
            V3 a, b, cc, d;
            int c0 = (k << 8) - 128 - (vcol & 255), c1 = c0 + 256, col = ((vcol >> 8) + k + ST_COLS) % ST_COLS;
            if (occupied(g, col, r - 1) && (r == ST_ROWS || occupied(g, col, r))) continue;   /* hidden behind blocks */
            tower_pt(c0, TOWER_R - 1, (int32_t)r * ROW_H - 1, &a); tower_pt(c1, TOWER_R - 1, (int32_t)r * ROW_H - 1, &b);
            tower_pt(c1, TOWER_R - 1, (int32_t)r * ROW_H + 1, &cc); tower_pt(c0, TOWER_R - 1, (int32_t)r * ROW_H + 1, &d);
            face(a, d, cc, b, PEN_CORE_LINE);
        }
    a3_flush(cv);
    a3_begin(1600);
    /* the blocks on the front half */
    for (r = 0; r < ST_TOTAL; r++)
        for (c = 0; c < ST_COLS; c++) {
            int v = g->board[r * ST_COLS + c], rel;
            if (!v) continue;
            rel = (((c << 8) - vcol + 8 * 256) & (16 * 256 - 1)) - 8 * 256;
            if (rel < -5 * 256 || rel > 5 * 256) continue;
            brick(rel, r, lit_pen(v), shade_pen(v), BRICK, board_nb(g, c, r));
        }
    /* the ghost, then the piece */
    if (!g->over) {
        Piece gp = g->piece;
        gp.row = st_ghost_row(g);
        ghost = stack.ghost && gp.row != g->piece.row;
        if (ghost) {
            n = st_cells_of(&gp, cells);
            for (k = 0; k < n; k++) {
                int rel = (((cells[k][0] << 8) - vcol + 8 * 256) & (16 * 256 - 1)) - 8 * 256;
                brick(rel, cells[k][1], PEN_CORE, lit_pen(g->piece.type + 1), 6, piece_nb(cells, n, k));   /* an outline in the piece's colour */
            }
        }
        n = st_cells_of(&g->piece, cells);
        for (k = 0; k < n; k++) {
            int rel = (((cells[k][0] << 8) - vcol + 8 * 256) & (16 * 256 - 1)) - 8 * 256;
            brick(rel, cells[k][1], lit_pen(g->piece.type + 1), shade_pen(g->piece.type + 1), BRICK + 6, piece_nb(cells, n, k));
        }
    }
    /* the red line at the top of the well */
    ring_band((int32_t)ST_ROWS * ROW_H - 2, (int32_t)ST_ROWS * ROW_H + 2, PEN_LINE, TOWER_R + 2);
    a3_flush(cv);
}

/* a piece as a little 2D icon */
static void icon(Canvas *cv, int type, int x, int y, int sz)
{
    int i;
    if (type < 0) return;
    for (i = 0; i < st_ncells[type]; i++) {
        int px = x + st_cells[type][i][0] * sz, py = y - st_cells[type][i][1] * sz;
        g_rect(cv, px, py, px + sz - 2, py + sz - 2, lit_pen(type + 1));
    }
}

/* the whole tower unrolled, centred on the piece */
static void map(Canvas *cv, const StGame *g, int x0, int y0)
{
    int c, r, cells[5][2], n, k, mid = g->piece.col;
    g_box(cv, x0 - 2, y0 - 2, x0 + ST_COLS * 3 + 1, y0 + ST_ROWS * 3 + 1, 0, 3);
    for (r = 0; r < ST_ROWS; r++)
        for (c = 0; c < ST_COLS; c++) {
            int col = st_mod(mid - 8 + c, ST_COLS), v = g->board[r * ST_COLS + col];
            if (v) g_rect(cv, x0 + c * 3, y0 + (ST_ROWS - 1 - r) * 3, x0 + c * 3 + 1, y0 + (ST_ROWS - 1 - r) * 3 + 1, lit_pen(v));
        }
    n = st_cells_of(&g->piece, cells);
    for (k = 0; k < n; k++) {
        int c2 = st_mod(cells[k][0] - mid + 8, ST_COLS), r2 = cells[k][1];
        if (r2 < ST_ROWS) g_rect(cv, x0 + c2 * 3, y0 + (ST_ROWS - 1 - r2) * 3, x0 + c2 * 3 + 1, y0 + (ST_ROWS - 1 - r2) * 3 + 1, 1);
    }
}

static void panel(Canvas *cv, const StGame *g)
{
    char tmp[32];
    int q[3], k;
    g_fill(cv, 200, 0, SCREEN_W - 1, SCREEN_H - 1, 2);
    g_sync();
    g_text(cv, 206, 12, 3, "SCORE");
    sprintf(tmp, "%ld", (long)g->score);
    g_text(cv, 206, 24, 1, tmp);
    sprintf(tmp, "LEVEL %ld", (long)g->level);
    g_text(cv, 206, 40, PEN_YELLOW, tmp);
    if (g->mode == MODE_DIG) sprintf(tmp, "DIG %ld", (long)g->garbage_left);
    else sprintf(tmp, "RINGS %ld", (long)g->lines);
    g_text(cv, 206, 52, PEN_CYAN, tmp);
    g_text(cv, 206, 70, 3, "NEXT");
    st_preview((StGame *)g, 3, q);
    for (k = 0; k < 3; k++) icon(cv, q[k], 222 + k * 30, 92, k ? 5 : 7);
    g_text(cv, 206, 112, 3, "HOLD");
    icon(cv, g->hold, 230, 132, 6);
    map(cv, g, 236, 150);
    sprintf(tmp, "%s", g->mode == MODE_MARATHON ? "MARATHON" : g->mode == MODE_RISING ? "RISING" : "DIG");
    g_text(cv, 206, 228, PEN_GREEN, tmp);
    sprintf(tmp, "%ld:%02ld", (long)(g->time_ms / 60000), (long)((g->time_ms / 1000) % 60));
    g_text(cv, 206, 242, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "PIECES LAND ON THE OUTSIDE OF A",
      "ROUND TOWER OF 16 COLUMNS. THERE ARE",
      "NO SIDE WALLS: LEFT AND RIGHT TURN",
      "THE TOWER UNDER THE PIECE.",
      "",
      "FILL A WHOLE RING TO CLEAR IT - THE",
      "BACK OF THE TOWER TOO. THE MAP SHOWS",
      "THE WHOLE TOWER UNROLLED.",
      "",
      "MARATHON: FASTER EVERY 10 RINGS",
      "RISING: RUBBLE PUSHES UP FROM BELOW",
      "DIG: CLEAR 10 RINGS OF RUBBLE" },
    { "LEFT / RIGHT   TURN THE TOWER",
#ifdef AMIGA3DO
      "UP             ROTATE CLOCKWISE",
#else
      "UP / X         ROTATE CLOCKWISE",
#endif
#ifdef AMIGA3DO
      "B              ROTATE ANTICLOCKWISE",
#else
      "Z              ROTATE ANTICLOCKWISE",
#endif
      "DOWN           SOFT DROP",
#ifdef AMIGA3DO
      "A              HARD DROP",
#else
      "SPACE / FIRE   HARD DROP",
#endif
      "C              HOLD",
#ifdef AMIGA3DO
      "L              GHOST ON / OFF",
#else
      "G              GHOST ON / OFF",
#endif
      "P              PAUSE",
#ifdef AMIGA3DO
      "R              THESE PAGES",
#else
      "HELP / H       THESE PAGES",
#endif
#ifdef AMIGA3DO
      "X              THE MENU",
#else
      "ESC            TITLE / QUIT",
#endif
      "",
      "SCORE: 100 300 600 1000 1500 x LEVEL" },
};

void render_help(Canvas *cv, int page)
{
    int k;
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, 0);
    g_sync();
    g_text_big(cv, page == 1 ? 72 : 96, 14, PEN_CYAN, page == 1 ? "HOW TO PLAY" : "CONTROLS", 2);
    for (k = 0; k < 12; k++) g_text(cv, 8, 58 + k * 14, 1, help_lines[page - 1][k]);
    g_text(cv, 8, 242, 3, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2");
#ifdef AMIGA3DO
    g_text(cv, 156, 242, PEN_YELLOW, "R: NEXT  A: PLAY");
#else
    g_text(cv, 156, 242, PEN_YELLOW, "HELP: NEXT  FIRE: PLAY");
#endif
}

/* AmigaOS 4's paintings (`make art`): a twilight landscape behind the
 * tower, and the title, in turn with the demo game */
static A3Image *backdrop, *title_img;
static int art_loaded, title_frames;
#define TITLE_ART_FRAMES  250           /* 10 s */
#define TITLE_DEMO_FRAMES 375

static void load_art(void)
{
    if (art_loaded) return;
    backdrop = g_image_load("PROGDIR:stack_backdrop.img");
    title_img = g_image_load("PROGDIR:stack_title.img");
    art_loaded = 1;
}

static void sky(Canvas *cv)
{
    load_art();
    if (backdrop) { g_image_draw(cv, backdrop, 0, 0); return; }
    g_fill(cv, 0, 0, 199, SCREEN_H - 1, PEN_SKY);
    g_sync();
}

void render_title_start(void) { title_frames = 0; }

void render_game(Canvas *cv, const StGame *g)
{
    sky(cv);
    draw_tower(cv, g);
    panel(cv, g);
    if (stack.state == SS_OVER) {
        char tmp[32];
        g_box(cv, 16, 90, 184, 160, 2, PEN_LINE);
        g_text_big(cv, 28, 98, g->won ? PEN_GREEN : PEN_LINE, g->won ? "DUG OUT!" : "TOPPED", 2);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text(cv, 40, 132, 1, tmp);
#ifdef AMIGA3DO
        if (stack.state_timer == 0) g_text(cv, 40, 150, PEN_YELLOW, "PRESS A");
#else
        if (stack.state_timer == 0) g_text(cv, 40, 150, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (stack.paused == 1) g_text_centre(cv, 120, 1, "PAUSED");
    if (stack.flash_timer > 0 && stack.flash[0]) g_text_shadow(cv, (200 - (int)strlen(stack.flash) * 8) / 2, 30, PEN_YELLOW, stack.flash);
}

void render_title(Canvas *cv, const StGame *g)
{
    char tmp[40];
    load_art();
    if (title_img && title_frames++ % (TITLE_ART_FRAMES + TITLE_DEMO_FRAMES) < TITLE_ART_FRAMES) {
        g_image_draw(cv, title_img, 0, 0);
        sprintf(tmp, "< %s >   LEVEL %ld (UP/DOWN)", stack.mode == MODE_MARATHON ? "MARATHON" : stack.mode == MODE_RISING ? "RISING" : "DIG",
                (long)stack.start_level);
        g_text_centre(cv, 214, PEN_GREEN, tmp);
#ifdef AMIGA3DO
        g_text_centre(cv, 230, (g->time_ms / 500) & 1 ? PEN_YELLOW : 1, "PRESS A TO START");
#else
        g_text_centre(cv, 230, (g->time_ms / 500) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO START");
#endif
#ifdef AMIGA3DO
        sprintf(tmp, "BEST %ld    R: RULES", (long)stack.best[stack.mode]);
#else
        sprintf(tmp, "BEST %ld    HELP: RULES", (long)stack.best[stack.mode]);
#endif
        g_text_centre(cv, 246, 3, tmp);
        return;
    }
    sky(cv);
    draw_tower(cv, g);                  /* the demo game behind */
    g_fill(cv, 200, 0, SCREEN_W - 1, SCREEN_H - 1, 2);
    g_sync();
    g_text_big(cv, 14, 14, 0, "STACK", 3);
    g_text_big(cv, 12, 12, PEN_YELLOW, "STACK", 3);
    g_text_big(cv, 14, 44, 0, "TOWER", 3);
    g_text_big(cv, 12, 42, PEN_CYAN, "TOWER", 3);
    g_text(cv, 206, 30, 3, "MODE");
    sprintf(tmp, "< %s >", stack.mode == MODE_MARATHON ? "MARATHON" : stack.mode == MODE_RISING ? "RISING" : "DIG");
    g_text(cv, 206, 44, PEN_GREEN, tmp);
    g_text(cv, 206, 66, 3, "LEVEL");
    sprintf(tmp, "  %ld  (UP/DOWN)", (long)stack.start_level);
    g_text(cv, 206, 80, 1, tmp);
    g_text(cv, 206, 110, 3, "BEST");
    sprintf(tmp, "%ld", (long)stack.best[stack.mode]);
    g_text(cv, 206, 124, PEN_YELLOW, tmp);
#ifdef AMIGA3DO
    g_text(cv, 206, 200, (g->time_ms / 500) & 1 ? PEN_YELLOW : 1, "A: START");
#else
    g_text(cv, 206, 200, (g->time_ms / 500) & 1 ? PEN_YELLOW : 1, "FIRE: START");
#endif
#ifdef AMIGA3DO
    g_text(cv, 206, 214, 3, "R: RULES");
#else
    g_text(cv, 206, 214, 3, "HELP: RULES");
#endif
    g_text(cv, 206, 242, 3, "AN AMIGA GAME");
}
