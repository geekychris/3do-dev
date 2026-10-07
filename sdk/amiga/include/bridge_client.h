/*
 * amiga_mcp bridge client API, mapped onto the 3DO debug console.
 *
 * AB_I/AB_W/AB_E are functions here (Norcroft has no variadic macros), so
 * existing calls like AB_I("score %d", s) compile unchanged and print to the
 * 3DO debug log (./3do log, emu_log). Variable/hook registration is a no-op;
 * use the emulator's symbol-aware tools instead (./3do ctl read_u32 addr=x).
 */
#ifndef BRIDGE_CLIENT_H
#define BRIDGE_CLIENT_H

#include "amiga_types.h"

#define AB_TYPE_I32 0
#define AB_TYPE_U32 1
#define AB_TYPE_I16 2
#define AB_TYPE_U16 3
#define AB_TYPE_I8  4
#define AB_TYPE_U8  5
#define AB_TYPE_STR 6
#define AB_TYPE_PTR 7

int  ab_init(const char *name);
void ab_cleanup(void);
void ab_poll(void);
void ab_log(const char *level, const char *fmt, ...);
void AB_I(const char *fmt, ...);
void AB_W(const char *fmt, ...);
void AB_E(const char *fmt, ...);
void AB_D(const char *fmt, ...);
void ab_register_var(const char *name, int type, void *ptr);
void ab_register_hook(const char *name, const char *desc, int (*fn)());
void ab_register_memregion(const char *name, void *ptr, ULONG size);
void ab_heartbeat(void);
void ab_push_var(const char *name);
int  ab_is_connected(void);

#endif
