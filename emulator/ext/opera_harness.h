/*
  tdo harness extension for opera-libretro

  Adds an out-of-band debug/automation API to the Opera core so that an
  external host (the `tdo` Python frontend / MCP server / GDB stub) can:

    * capture the 3DO debug console (kernel kprintf/printf output, which
      the OS writes byte-by-byte to the MADAM ID register) as a text stream.
      Optionally also decode kprintf SWIs (0x1000E) directly.
    * trace SWI calls (number, pc, r0-r3)
    * set PC tracepoints that snapshot registers when an address executes
    * debug: halting breakpoints, data watchpoints, single-step, async halt,
      register writes. While halted, retro_run() returns without running
      the CPU; the interrupted frame resumes exactly where it stopped.
    * read/write guest DRAM/VRAM and read ROM

  All exported symbols are prefixed `tdo_` and are plain C so they can be
  loaded with ctypes / dlsym alongside the normal libretro API.
*/
#ifndef OPERA_HARNESS_H_INCLUDED
#define OPERA_HARNESS_H_INCLUDED

#include <stdint.h>
#include <stddef.h>

#include "extern_c.h"

EXTERN_C_BEGIN

#define TDO_HARNESS_VERSION 2

/* stop reasons (tdo_dbg_stop_info) */
#define TDO_STOP_NONE      0
#define TDO_STOP_HALT      1  /* host asked (GDB ^C)  -> SIGINT  */
#define TDO_STOP_BREAK     2  /* breakpoint           -> SIGTRAP */
#define TDO_STOP_STEP      3  /* single step done     -> SIGTRAP */
#define TDO_STOP_WATCH     4  /* data watchpoint      -> SIGTRAP */

/* watchpoint types */
#define TDO_WATCH_WRITE    1
#define TDO_WATCH_READ     2
#define TDO_WATCH_ACCESS   3

/* ---- called from the core (internal) ---- */
extern int g_tdo_exec_hooks;   /* nonzero: call tdo_harness_before_exec */
extern int g_tdo_watch_active; /* nonzero: call tdo_harness_on_access   */
void tdo_harness_debug_putc(char c_);
void tdo_harness_on_swi(uint32_t swi_);
int  tdo_harness_before_exec(uint32_t pc_);  /* 1 = halt now, don't execute */
void tdo_harness_on_access(uint32_t addr_, uint32_t size_, int write_);
void tdo_harness_on_frame(void);
void tdo_harness_on_reset(void);

/* accessors implemented in opera_arm.c */
uint32_t opera_arm_harness_reg(int n_);
uint32_t opera_arm_harness_cpsr(void);
void     opera_arm_harness_set_reg(int n_, uint32_t v_);
void     opera_arm_harness_set_cpsr(uint32_t v_);
uint32_t opera_arm_harness_peek8(uint32_t addr_, int *ok_);
int      opera_arm_harness_poke8(uint32_t addr_, uint8_t val_);

/* ---- exported API (for the host) ---- */
uint32_t tdo_version(void);
uint64_t tdo_frame_count(void);

/* Drain captured debug console text. Returns number of bytes copied. */
size_t   tdo_debug_read(char *buf_, size_t max_);

/* Decode kprintf SWIs host-side as well (default off; may duplicate
   output on ROMs whose kernel also writes to the debug port). */
void     tdo_kprintf_swi_enable(int enable_);

/* out_ must hold 17 words: r0..r15, cpsr. r15 is the address of the next
   instruction to execute. */
void     tdo_get_regs(uint32_t *out_);
/* n_: 0..15 = r0..r15 (current mode bank), 16 = cpsr */
void     tdo_set_reg(int n_, uint32_t v_);

/* Byte-addressed guest memory access (big-endian guest view). */
uint32_t tdo_mem_read(uint32_t addr_, uint8_t *out_, uint32_t len_);
uint32_t tdo_mem_write(uint32_t addr_, const uint8_t *in_, uint32_t len_);

/* SWI trace: entries are 7 words: frame_lo, swi, pc, r0, r1, r2, r3 */
void     tdo_swi_trace_enable(int enable_);
uint32_t tdo_swi_trace_read(uint32_t *out_, uint32_t max_entries_);

/* Tracepoints (log, don't stop): entries are 10 words:
   frame_lo, pc, r0, r1, r2, r3, sp, lr, cpsr, hit_count */
int      tdo_trace_add(uint32_t addr_);
int      tdo_trace_remove(uint32_t addr_);
void     tdo_trace_clear(void);
uint32_t tdo_trace_read(uint32_t *out_, uint32_t max_entries_);

/* Debugger */
void     tdo_dbg_halt(void);       /* stop before the next instruction */
void     tdo_dbg_continue(void);   /* resume (steps over a bp at pc)   */
void     tdo_dbg_step(void);       /* run exactly one instruction       */
int      tdo_dbg_halted(void);
/* out_[0]=reason, [1]=pc, [2]=watch addr, [3]=watch type */
void     tdo_dbg_stop_info(uint32_t *out_);
int      tdo_bp_add(uint32_t addr_);
int      tdo_bp_remove(uint32_t addr_);
void     tdo_bp_clear(void);
int      tdo_wp_add(uint32_t addr_, uint32_t len_, int type_);
int      tdo_wp_remove(uint32_t addr_, uint32_t len_, int type_);
void     tdo_wp_clear(void);

EXTERN_C_END

#endif
