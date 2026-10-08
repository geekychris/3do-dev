/*
 * The room on screen, as a list of cels (3DO headers only).
 *
 *   background   the floor and back walls, drawn once per room (vox.c)
 *   items        blocks, spikes, gates, the throne, lifts, pickups, crates
 *                and characters: pre-rendered sprites, drawn back to front
 *   arches       the front doorways, always in front
 *   flash, panel full-screen colour flash and the status panel's shade:
 *                1x1 cels whose pixel processor mixes with the frame buffer
 *
 * Draw order: an item must come after every item that is behind it and
 * overlaps it on screen. "Behind" for two boxes is any separating axis
 * that puts one further from the camera (+x, +z) or below the other.
 * Static pairs are worked out at room entry; the few moving items are
 * linked in each frame, then a depth-first walk emits the cels.
 */
#include "graphics.h"
#include "celutils.h"
#include "keep.h"

#define MAXITEMS  160
#define MAXEDGES  1400
#define MAXCELS   260

int scene_ox, scene_oy, scene_ncels;

static CCB *bg_cel, *panel_cel, *flash_cel;
static uint32 bg_pix[2], panel_pix[2], flash_pix[2];
#define MAXBG 200
static CCB bgcels[MAXBG];           /* the room's floor and back walls, made once per room */
static int nbg;
static CCB pool[MAXCELS];
static int npool;

enum { I_BLOCK, I_SPIKES, I_GATE, I_THRONE, I_ACTOR, I_LIFT, I_PICKUP };
typedef struct {
    Box b;
    short x0, y0, x1, y1;           /* screen rectangle */
    long key;                       /* depth of the centre: bigger = further */
    short kind, idx, var;
    short cx, cy, cz;               /* static items: the cell */
    short head;                     /* static edges: first */
    short dhead;                    /* this frame's edges: first */
    unsigned char mark;
} DItem;
static DItem item[MAXITEMS];
static int nstatic, nitem;
typedef struct { short from, next; } Edge;      /* `from` is drawn before the list's owner */
static Edge sedge[MAXEDGES], dedge[MAXEDGES];
static int nsedge, ndedge;

/* ---- cels ---- */

static CCB *make_cel(Sprite *s)
{
    CCB *c;
    if (!s->pix || s->w <= 0) return 0;
    if (s->ccb) return (CCB *)s->ccb;
    c = CreateCel(s->w, s->h, 16, CREATECEL_UNCODED, s->pix);
    if (!c) return 0;
    c->ccb_Flags = (c->ccb_Flags | CCB_NPABS | CCB_SPABS) & ~(CCB_BGND | CCB_LAST);
    s->ccb = c;
    return c;
}

static void forget_cel(Sprite *s)
{
    if (s->ccb) DeleteCel((CCB *)s->ccb);
    s->ccb = 0;
}

/* sprite with its model origin at screen pixel (x, y) */
static void emit(Sprite *s, long x, long y, uint32 pixc)
{
    CCB *t = make_cel(s), *c;
    if (!t || npool >= MAXCELS) return;
    c = &pool[npool];
    *c = *t;
    c->ccb_XPos = (x - s->ax) << 16;
    c->ccb_YPos = (y - s->ay) << 16;
    if (pixc) c->ccb_PIXC = pixc;
    c->ccb_NextPtr = &pool[npool + 1];
    npool++;
}

static void emit_cel(CCB *t)
{
    if (!t || npool >= MAXCELS) return;
    pool[npool] = *t;
    pool[npool].ccb_Flags = (t->ccb_Flags | CCB_NPABS | CCB_SPABS) & ~CCB_LAST;
    pool[npool].ccb_NextPtr = &pool[npool + 1];
    npool++;
}

static CCB *rect_cel(uint32 *pix, long x, long y, long w, long h, uint32 pixc)
{
    CCB *c = CreateCel(1, 1, 16, CREATECEL_UNCODED, pix);
    if (!c) return 0;
    c->ccb_Flags = (c->ccb_Flags | CCB_NPABS | CCB_SPABS | CCB_BGND) & ~CCB_LAST;
    c->ccb_XPos = x << 16;
    c->ccb_YPos = y << 16;
    c->ccb_HDX = w << 20;
    c->ccb_VDY = h << 16;
    c->ccb_PIXC = pixc;
    return c;
}

/* fb * 4/8: shadows */
#define PIXC_SHADOW 0x8F008F00UL
/* fb * 3/8: the status panel */
#define PIXC_PANEL  0x8B008B00UL
/* fb * 7/8 + colour / 2: a flash */
#define PIXC_FLASH  0x9BC19BC1UL

void scene_init(void)
{
    /* the camera's background colour (0.07, 0.075, 0.1) */
    bg_pix[0] = bg_pix[1] = ((uint32)(((18 >> 3) << 10) | ((19 >> 3) << 5) | (26 >> 3)) << 16) |
                            (((18 >> 3) << 10) | ((19 >> 3) << 5) | (26 >> 3));
    bg_cel = rect_cel(bg_pix, 0, 0, 320, 240, 0x1F001F00UL);
    panel_pix[0] = panel_pix[1] = 0x00010001UL;
    panel_cel = rect_cel(panel_pix, 0, 200, 320, 40, PIXC_PANEL);
    flash_pix[0] = flash_pix[1] = 0x00010001UL;
    flash_cel = rect_cel(flash_pix, 0, 0, 320, 240, PIXC_FLASH);
}

/* ---- projection ---- */

static long sx_of(fix x, fix z) { return scene_ox + (((x - z) * ISO_X) >> 12); }
static long sy_of(fix x, fix y, fix z) { return scene_oy - ((y * ISO_Y + (x + z) * ISO_Z) >> 12); }

static void set_box(DItem *it, fix x0, fix y0, fix z0, fix x1, fix y1, fix z1)
{
    it->b.x0 = x0; it->b.y0 = y0; it->b.z0 = z0;
    it->b.x1 = x1; it->b.y1 = y1; it->b.z1 = z1;
    it->x0 = (short)(sx_of(x0, z1) - 2);
    it->x1 = (short)(sx_of(x1, z0) + 2);
    it->y0 = (short)(sy_of(x1, y1, z1) - 2);
    it->y1 = (short)(sy_of(x0, y0, z0) + 2);
    it->key = ((x0 + x1 + z0 + z1) * 17 - (y0 + y1) * 14) >> 1;
}

static int rects_meet(const DItem *a, const DItem *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

/* 1: a before b, -1: b before a, 0: either */
static int order(const DItem *a, const DItem *b)
{
    const fix e = 4;
    int ab = a->b.x0 >= b->b.x1 - e || a->b.z0 >= b->b.z1 - e || a->b.y1 <= b->b.y0 + e;
    int ba = b->b.x0 >= a->b.x1 - e || b->b.z0 >= a->b.z1 - e || b->b.y1 <= a->b.y0 + e;
    if (ab && !ba) return 1;
    if (ba && !ab) return -1;
    if (a->key != b->key) return a->key > b->key ? 1 : -1;
    return 0;
}

static void add_edge(Edge *pool_, int *n, short *head, int from)
{
    if (*n >= MAXEDGES) return;
    pool_[*n].from = (short)from;
    pool_[*n].next = *head;
    *head = (short)(*n)++;
}

/* ---- a new room ---- */

static void bg_add(Sprite *s, long x, long y)
{
    CCB *t = make_cel(s);
    if (!t || nbg >= MAXBG) return;
    bgcels[nbg] = *t;
    bgcels[nbg].ccb_XPos = (x - s->ax) << 16;
    bgcels[nbg].ccb_YPos = (y - s->ay) << 16;
    bgcels[nbg].ccb_NextPtr = &bgcels[nbg + 1];
    nbg++;
}

#define BX(x, z) (scene_ox + ISO_X * ((x) - (z)))
#define BY(x, y, z) (scene_oy - ISO_Y * (y) - ISO_Z * ((x) + (z)))

/* the floor and back walls, back to front: walls by distance (x + z) then
 * height, the corner post first; then the door steps; then the floor */
static void background(void)
{
    const KRoom *r = R.room;
    int x, y, z, k, door[2], lo[2], hi[2];
    nbg = 0;
    door[0] = lv_has_door(R.level, R.ridx, SIDE_N);
    door[1] = lv_has_door(R.level, R.ridx, SIDE_E);
    door_span(r->w, &lo[0], &hi[0]);
    door_span(r->d, &lo[1], &hi[1]);
    for (k = r->w + r->d; k >= 0; k--)
        for (y = 0; y < 3; y++) {
            if (k == r->w + r->d) bg_add(&spr_brick[VARIANTS / 2], BX(r->w, r->d), BY(r->w, y, r->d));
            x = k - r->d;                                   /* north wall: (x, y, d) */
            if (x >= 0 && x < r->w && !(door[0] && x >= lo[0] && x < hi[0] && y < 2))
                bg_add(&spr_brick[brick_variant(x, y, 0)], BX(x, r->d), BY(x, y, r->d));
            z = k - r->w;                                   /* east wall: (w, y, z) */
            if (z >= 0 && z < r->d && !(door[1] && z >= lo[1] && z < hi[1] && y < 2))
                bg_add(&spr_brick[brick_variant(z, y, 1)], BX(r->w, z), BY(r->w, y, z));
        }
    if (door[0]) bg_add(&spr_step[0], scene_ox, scene_oy);
    if (door[1]) bg_add(&spr_step[1], scene_ox, scene_oy);
    for (k = r->w + r->d - 2; k >= 0; k--)
        for (x = 0; x < r->w; x++) {
            z = k - x;
            if (z >= 0 && z < r->d) bg_add(&spr_tile[tile_variant(x, z)], BX(x, z), BY(x, 0, z));
        }
}

static void add_static(int kind, int x, int y, int z, int var)
{
    DItem *it;
    fix X = (fix)x << 12, Y = (fix)y << 12, Z = (fix)z << 12;
    if (nstatic >= MAXITEMS - MAXACT - MAXLIFT - MAXPICK) return;
    it = &item[nstatic++];
    it->kind = (short)kind;
    it->var = (short)var;
    it->idx = 0;
    switch (kind) {
    case I_SPIKES: set_box(it, X, Y, Z, X + FX, Y + 1843, Z + FX); break;
    case I_GATE:   set_box(it, X, Y, Z, X + FX, Y + 2 * FX, Z + FX); break;
    case I_THRONE: set_box(it, X + 410, Y, Z + 614, X + 3686, Y + 6800, Z + 3564); break;
    default:       set_box(it, X, Y, Z, X + FX, Y + FX, Z + FX); break;
    }
    it->cx = (short)x;
    it->cy = (short)y;
    it->cz = (short)z;
}

void scene_room(void)
{
    const KRoom *r = R.room;
    int x, y, z, i, j;
    /* frame the room above the panel (Game.FrameRoom) */
    scene_ox = 160 + ISO_X * (r->d - r->w) / 2;
    scene_oy = 100 + (3 * ISO_Y + ISO_Z * (r->w + r->d + 2)) / 2;
    for (i = 0; i < 2; i++)
        for (j = 0; j < BLOCK_VARIANTS; j++) forget_cel(&spr_block[i][j]);
    forget_cel(&spr_arch[0]);
    forget_cel(&spr_arch[1]);
    for (i = 0; i < VARIANTS; i++) { forget_cel(&spr_tile[i]); forget_cel(&spr_brick[i]); }
    forget_cel(&spr_step[0]);
    forget_cel(&spr_step[1]);
    vox_room(r);
    background();

    nstatic = 0;
    for (y = 0; y < MAXH; y++)
        for (z = 0; z < r->d; z++)
            for (x = 0; x < r->w; x++)
                switch (CELL(r, x, y, z)) {
                case '#': add_static(I_BLOCK, x, y, z, block_variant(x, y, z)); break;
                case 'B': add_static(I_BLOCK, x, y, z, BLOCK_VARIANTS + block_variant(x, y, z)); break;
                case '^': add_static(I_SPIKES, x, y, z, 0); break;
                case 'G': add_static(I_GATE, x, y, z, x == 0 || x == r->w - 1); break;
                case 'T': add_static(I_THRONE, x, y, z, 0); break;
                }
    /* far ones first, so ties come out in a sensible order */
    for (i = 1; i < nstatic; i++) {
        DItem t = item[i];
        for (j = i; j > 0 && item[j - 1].key < t.key; j--) item[j] = item[j - 1];
        item[j] = t;
    }
    nsedge = 0;
    for (i = 0; i < nstatic; i++) item[i].head = -1;
    for (i = 0; i < nstatic; i++)
        for (j = i + 1; j < nstatic; j++) {
            int o;
            if (!rects_meet(&item[i], &item[j])) continue;
            o = order(&item[i], &item[j]);
            if (o > 0) add_edge(sedge, &nsedge, &item[j].head, i);
            else if (o < 0) add_edge(sedge, &nsedge, &item[i].head, j);
        }
}

/* ---- each frame ---- */

extern long isin256(int a);

static void actor_item(int a)
{
    DItem *it = &item[nitem];
    Box b;
    body_box(R.act[a].body, &b);
    it->kind = I_ACTOR;
    it->idx = (short)a;
    set_box(it, b.x0, b.y0, b.z0, b.x1, b.y1, b.z1);
    nitem++;
}

static void draw_item(const DItem *it, int show_player)
{
    static const unsigned char walk_frame[4] = { 0, 1, 0, 2 };
    switch (it->kind) {
    case I_BLOCK: {
        int x = it->cx, y = it->cy, z = it->cz;
        Sprite *s = it->var >= BLOCK_VARIANTS ? &spr_block[1][it->var - BLOCK_VARIANTS] : &spr_block[0][it->var];
        emit(s, sx_of((fix)x << 12, (fix)z << 12), sy_of((fix)x << 12, (fix)y << 12, (fix)z << 12), 0);
        break;
    }
    case I_SPIKES:
    case I_GATE:
    case I_THRONE: {
        fix x = ((fix)it->cx << 12) + FX / 2, y = (fix)it->cy << 12, z = ((fix)it->cz << 12) + FX / 2;
        Sprite *s = it->kind == I_SPIKES ? &spr_spikes : it->kind == I_GATE ? &spr_gate[it->var] : &spr_throne;
        if (it->kind == I_GATE && R.ngate == 0) break;                /* opened */
        emit(s, sx_of(x, z), sy_of(x, y, z), 0);
        break;
    }
    case I_LIFT: {
        const Lift *l = &R.lift[it->idx];
        const Body *b = l->body;
        int k = (int)((b->py - l->base + FX / 4 - 1) / (FX / 4));
        if (k > 8) k = 8;
        if (k > 0) emit(&spr_piston[k], sx_of(b->px, b->pz), sy_of(b->px, b->py, b->pz), 0);
        emit(&spr_lift, sx_of(b->px, b->pz), sy_of(b->px, b->py, b->pz), 0);
        break;
    }
    case I_PICKUP: {
        const Pickup *p = &R.pk[it->idx];
        fix x = ((fix)p->x << 12) + FX / 2, y = (fix)p->y << 12, z = ((fix)p->z << 12) + FX / 2;
        /* Pickup.Animate: phase = x * 1.7 + z (centre of the cell); bob sin(3t + phase) * 0.08,
         * spin 140 t + 30 phase degrees. In 1/256 turns and 22.5-degree frames: */
        long ph = 693L * p->x / 10 + 407L * p->z / 10 + 55;
        long bob = (isin256((int)((R.clock * 2444 / 1000 + ph) & 255)) * 22) >> 14;   /* px * 16 */
        int f = (int)((R.clock * 1244 / 10000 + 227L * p->x / 100 + 133L * p->z / 100 + 2) & 15);
        Sprite *s = p->kind == 'R' ? &spr_relic[f] : p->kind == 'K' ? &spr_key[f] : &spr_potion;
        emit(s, sx_of(x, z), sy_of(x, y, z) - bob / 16, 0);
        break;
    }
    case I_ACTOR: {
        const Actor *a = &R.act[it->idx];
        const Body *b = a->body;
        long x = sx_of(b->px, b->pz), y = sy_of(b->px, b->py, b->pz);
        int fr = walk_frame[((a->phase >> 8) + 32) >> 6 & 3];
        Sprite *s = 0;
        if (a->kind == K_PLAYER && (!show_player || !a->visible)) break;
        if (a->kind != K_CRATE) {
            fix g = w_ground_below(b->px, b->py, b->pz);
            emit(&spr_shadow[1], sx_of(b->px, b->pz), sy_of(b->px, g, b->pz), PIXC_SHADOW);
        }
        switch (a->kind) {
        case K_PLAYER:  s = &spr_walker[0][a->face][fr]; break;
        case K_GUARD:   s = &spr_walker[1][a->face][fr]; break;
        case K_HOUND:   s = &spr_walker[2][a->face][fr]; break;
        case K_SAGE:    s = &spr_sage[a->face]; break;
        case K_GHOST:   s = &spr_ghost[a->face]; break;
        case K_BOUNCER: s = &spr_bouncer[a->roll & 7]; break;
        case K_CRATE:   s = &spr_crate; break;
        }
        if (s) emit(s, x, y, 0);
        break;
    }
    }
}

static int show_player_flag;

static void visit(int i)
{
    int e;
    item[i].mark = 1;
    if (i < nstatic)
        for (e = item[i].head; e >= 0; e = sedge[e].next)
            if (!item[sedge[e].from].mark) visit(sedge[e].from);
    for (e = item[i].dhead; e >= 0; e = dedge[e].next)
        if (!item[dedge[e].from].mark) visit(dedge[e].from);
    draw_item(&item[i], show_player_flag);
}

void *scene_frame(int show_player, int panel, int flash_ink)
{
    int i, j;
    npool = 0;
    show_player_flag = show_player;

    /* the moving things */
    nitem = nstatic;
    for (i = 0; i < R.nlift; i++) {
        const Lift *l = &R.lift[i];
        Box b;
        body_box(l->body, &b);
        item[nitem].kind = I_LIFT;
        item[nitem].idx = (short)i;
        set_box(&item[nitem], b.x0, l->base, b.z0, b.x1, b.y1, b.z1);
        nitem++;
    }
    for (i = 0; i < R.npk; i++) {
        item[nitem].kind = I_PICKUP;
        item[nitem].idx = (short)i;
        set_box(&item[nitem], R.pk[i].box.x0, R.pk[i].box.y0, R.pk[i].box.z0,
                R.pk[i].box.x1, R.pk[i].box.y1, R.pk[i].box.z1);
        nitem++;
    }
    for (i = 0; i < R.nact; i++) actor_item(i);

    ndedge = 0;
    for (i = 0; i < nitem; i++) {
        item[i].dhead = -1;
        item[i].mark = 0;
    }
    for (i = nstatic; i < nitem; i++)
        for (j = 0; j < nitem; j++) {
            int o;
            if (j == i || (j >= nstatic && j < i) || !rects_meet(&item[i], &item[j])) continue;
            o = order(&item[i], &item[j]);
            if (o > 0) add_edge(dedge, &ndedge, &item[j].dhead, i);
            else if (o < 0) add_edge(dedge, &ndedge, &item[i].dhead, j);
        }
    for (i = 0; i < nitem; i++)
        if (!item[i].mark) visit(i);

    if (spr_arch[0].pix && lv_has_door(R.level, R.ridx, SIDE_S)) emit(&spr_arch[0], scene_ox, scene_oy, 0);
    if (spr_arch[1].pix && lv_has_door(R.level, R.ridx, SIDE_W)) emit(&spr_arch[1], scene_ox, scene_oy, 0);
    if (flash_ink >= 0 && flash_cel) {
        unsigned short c = ink_rgb15(flash_ink, 1);
        /* the cel adds colour / 2: store a third of the ink for ~16% */
        unsigned short q = (unsigned short)((((c >> 10) & 31) / 3 << 10) | (((c >> 5) & 31) / 3 << 5) | ((c & 31) / 3));
        flash_pix[0] = flash_pix[1] = ((uint32)q << 16) | q;
        emit_cel(flash_cel);
    }
    if (panel && panel_cel) emit_cel(panel_cel);
    scene_ncels = npool + nbg + 1;
    /* background colour, then the room's walls and floor, then this frame's cels */
    if (npool) pool[npool - 1].ccb_Flags |= CCB_LAST;
    if (nbg) {
        bgcels[nbg - 1].ccb_NextPtr = npool ? pool : 0;
        if (npool) bgcels[nbg - 1].ccb_Flags &= ~CCB_LAST;
        else bgcels[nbg - 1].ccb_Flags |= CCB_LAST;
    }
    if (!bg_cel) return npool ? (void *)pool : (nbg ? (void *)bgcels : 0);
    bg_cel->ccb_NextPtr = nbg ? bgcels : pool;
    if (!nbg && !npool) bg_cel->ccb_Flags |= CCB_LAST;
    else bg_cel->ccb_Flags &= ~CCB_LAST;
    return bg_cel;
}
