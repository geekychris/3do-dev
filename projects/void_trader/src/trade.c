// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "trade.h"

#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include <string.h>
#include <stdio.h>

const Commodity vt_commodities[VT_COMMODITY_COUNT] = {
    { "Food",        7   },
    { "Textiles",   12   },
    { "Machinery",  56   },
    { "Computers", 108   },
    { "Weapons",   285   },
    { "Alien Tech",640   },
};

/* Palette pens (set up in main.c's install_palette) */
#define PEN_MENU_BG   122     /* dark grey backdrop */
#define PEN_TEXT      120     /* phosphor green */
#define PEN_DIM       121
#define PEN_HL        123     /* highlight */
#define PEN_ALERT     124     /* red — can't afford / no cargo */

void vt_trade_init(TradeState *t)
{
    memset(t, 0, sizeof(*t));
    t->credits = 100;        /* starting stake */
    t->cursor  = 0;
}

LONG vt_cargo_total(const TradeState *t)
{
    LONG n = 0;
    int i;
    for (i = 0; i < VT_COMMODITY_COUNT; i++) n += t->cargo[i];
    return n;
}

void vt_trade_menu(TradeState *t, int event)
{
    switch (event) {
    case VT_MENU_UP:
        t->cursor = (t->cursor + VT_COMMODITY_COUNT - 1) % VT_COMMODITY_COUNT;
        break;
    case VT_MENU_DOWN:
        t->cursor = (t->cursor + 1) % VT_COMMODITY_COUNT;
        break;
    case VT_MENU_BUY: {
        const Commodity *c = &vt_commodities[t->cursor];
        if (t->credits >= c->price
            && vt_cargo_total(t) < VT_CARGO_MAX) {
            t->credits -= c->price;
            if (t->cargo[t->cursor] < 250) t->cargo[t->cursor]++;
        }
        break;
    }
    case VT_MENU_SELL: {
        const Commodity *c = &vt_commodities[t->cursor];
        if (t->cargo[t->cursor] > 0) {
            t->cargo[t->cursor]--;
            /* Station buys back at 90% to make trading routes
             * meaningful when Phase 6 adds price variance. */
            t->credits += (c->price * 9) / 10;
        }
        break;
    }
    default: break;
    }
}

void vt_trade_render(struct RastPort *rp, const TradeState *t)
{
    int i;
    char buf[48];

    /* Backdrop */
    SetAPen(rp, PEN_MENU_BG);
    RectFill(rp, 0, 0, 319, 255);

    /* Header */
    SetAPen(rp, PEN_TEXT);
    SetDrMd(rp, JAM1);
    Move(rp, 92, 12);
    Text(rp, (STRPTR)"STATION  MARKET", 15);

    /* Column headings */
    SetAPen(rp, PEN_DIM);
    Move(rp, 24,  32); Text(rp, (STRPTR)"COMMODITY", 9);
    Move(rp, 144, 32); Text(rp, (STRPTR)"PRICE",     5);
    Move(rp, 200, 32); Text(rp, (STRPTR)"HOLD",      4);

    /* Rows */
    for (i = 0; i < VT_COMMODITY_COUNT; i++) {
        int y = 48 + i * 14;
        int is_cursor = (i == t->cursor);
        UBYTE pen = is_cursor ? PEN_HL : PEN_TEXT;

        /* Cursor arrow */
        if (is_cursor) {
            SetAPen(rp, PEN_HL);
            Move(rp, 12, y); Text(rp, (STRPTR)">", 1);
        }
        SetAPen(rp, pen);
        Move(rp, 24,  y); Text(rp, (STRPTR)vt_commodities[i].name,
                                strlen(vt_commodities[i].name));
        /* amiga.lib sprintf %d reads 16-bit — CLAUDE.md gotcha —
         * so always cast to long and use %ld here. */
        sprintf(buf, "%4ld", (long)vt_commodities[i].price);
        Move(rp, 144, y); Text(rp, (STRPTR)buf, 4);
        sprintf(buf, "%3ld", (long)t->cargo[i]);
        Move(rp, 200, y); Text(rp, (STRPTR)buf, 3);
    }

    /* Footer: credits + cargo total */
    SetAPen(rp, PEN_TEXT);
    sprintf(buf, "CREDITS %5ld", (long)t->credits);
    Move(rp, 24, 176); Text(rp, (STRPTR)buf, strlen(buf));
    sprintf(buf, "CARGO   %2ld/%ld",
            (long)vt_cargo_total(t), (long)VT_CARGO_MAX);
    Move(rp, 176, 176); Text(rp, (STRPTR)buf, strlen(buf));

    /* Controls hint */
    SetAPen(rp, PEN_DIM);
    Move(rp, 24, 210); Text(rp, (STRPTR)"W/S select   B buy   N sell", 27);
    Move(rp, 24, 224); Text(rp, (STRPTR)"U undock and launch",         19);
}
