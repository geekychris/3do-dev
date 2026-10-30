/*
 * bridge_client.h - the amiga_mcp bridge client API for the amiga3d games
 * on the 3DO (sdk/amiga3d). Logging goes to the 3DO debug console through
 * sdk/amiga's AB_I/AB_W/AB_E; variables, hooks and memory regions are
 * no-ops (read globals with the emulator's symbol-aware tools instead);
 * the test harness calls log their results (main_3do.c).
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

typedef int (*ab_hook_fn_t)(const char *args, char *resultBuf, int bufSize);

int  ab_init(const char *name);
void ab_cleanup(void);
void ab_poll(void);
void AB_I(const char *fmt, ...);
void AB_W(const char *fmt, ...);
void AB_E(const char *fmt, ...);
void AB_D(const char *fmt, ...);
void ab_register_var(const char *name, int type, void *ptr);
void ab_register_hook(const char *name, const char *desc, int (*fn)());
#define ab_register_memregion(name, addr, size, desc) ((void)(addr))

void ab_test_begin(const char *suiteName);
int  ab_test_assert(int condition, const char *testName, const char *file, int line);
void ab_test_end(void);
#define AB_ASSERT(cond, name) ab_test_assert((cond), (name), __FILE__, __LINE__)

#endif
