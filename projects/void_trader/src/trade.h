// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef VT_TRADE_H
#define VT_TRADE_H

#include <exec/types.h>

/*
 * Trading model — minimalist Elite: 6 commodities, per-station
 * fixed prices, one shared cargo hold + credits. Buy raises your
 * cargo, spends credits; sell drops cargo, earns credits.
 *
 * Prices vary per station. Currently we only have one station,
 * so the price table is static; Phase 6 (hyperspace) would
 * layer per-system pricing on top.
 */

#define VT_COMMODITY_COUNT   6
#define VT_CARGO_MAX         32

typedef struct {
    const char *name;
    UWORD price;         /* credits per unit at this station */
} Commodity;

extern const Commodity vt_commodities[VT_COMMODITY_COUNT];

typedef struct {
    LONG  credits;
    UBYTE cargo[VT_COMMODITY_COUNT];   /* units per commodity */
    UBYTE cursor;                       /* highlighted row in menu */
    UBYTE _pad[2];
} TradeState;

void vt_trade_init(TradeState *t);

/* Return total cargo held. */
LONG vt_cargo_total(const TradeState *t);

/* Input handling for the docked screen. Returns 0 normally, 1 if
 * the player just pressed U (main.c reads it to trigger undock).
 * up/down/buy/sell are edge-triggered by the caller. */
enum {
    VT_MENU_NONE = 0,
    VT_MENU_UP,
    VT_MENU_DOWN,
    VT_MENU_BUY,
    VT_MENU_SELL,
};
void vt_trade_menu(TradeState *t, int event);

/* Render the market UI over the docked screen backdrop. */
struct RastPort;
void vt_trade_render(struct RastPort *rp, const TradeState *t);

#endif
