/*
  tdo harness extension for opera-libretro. See opera_harness.h.
*/
#include "opera_harness.h"

#include <stdio.h>
#include <string.h>

#define DEBUG_RING_SIZE   (64 * 1024)
#define SWI_RING_ENTRIES  4096
#define SWI_ENTRY_WORDS   7
#define TRACE_MAX_POINTS  64
#define TRACE_RING_ENTRIES 4096
#define TRACE_ENTRY_WORDS 10

#define SWI_KPRINTF 0x1000E

static uint64_t s_frame;
static int      s_kprintf_swi;

/* ---- debug text ring ---- */
static char     s_dbg[DEBUG_RING_SIZE];
static uint32_t s_dbg_head; /* write */
static uint32_t s_dbg_tail; /* read */

/* ---- swi ring ---- */
static int      s_swi_enabled;
static uint32_t s_swi[SWI_RING_ENTRIES * SWI_ENTRY_WORDS];
static uint32_t s_swi_head;
static uint32_t s_swi_count;

/* ---- tracepoints ---- */
static uint32_t s_tp_addr[TRACE_MAX_POINTS];
static uint32_t s_tp_hits[TRACE_MAX_POINTS];
static int      s_tp_n;
static uint32_t s_tr[TRACE_RING_ENTRIES * TRACE_ENTRY_WORDS];
static uint32_t s_tr_head;
static uint32_t s_tr_count;

/* ---- debugger ---- */
#define BP_MAX 64
#define WP_MAX 16
int             g_tdo_exec_hooks;
int             g_tdo_watch_active;
static uint32_t s_bp[BP_MAX];
static int      s_bp_n;
static struct { uint32_t addr, len; int type; } s_wp[WP_MAX];
static int      s_wp_n;
static int      s_halted;
static int      s_halt_req;
static int      s_step;          /* 1 = execute one, 2 = stop now */
static int      s_skip_valid;    /* resume over a breakpoint at pc */
static uint32_t s_skip_pc;
static int      s_watch_hit;
static uint32_t s_watch_addr;
static int      s_watch_type;
static uint32_t s_fetch_pc;
static uint32_t s_stop[4];

static void update_hooks(void);

uint32_t tdo_version(void)     { return TDO_HARNESS_VERSION; }
uint64_t tdo_frame_count(void) { return s_frame; }
void     tdo_harness_on_frame(void) { s_frame++; }

static void
dbg_putc(char c_)
{
  uint32_t next = (s_dbg_head + 1) % DEBUG_RING_SIZE;
  if(next == s_dbg_tail) /* full: drop oldest */
    s_dbg_tail = (s_dbg_tail + 1) % DEBUG_RING_SIZE;
  s_dbg[s_dbg_head] = c_;
  s_dbg_head = next;
}

void
tdo_harness_debug_putc(char c_)
{
  dbg_putc(c_);
}

void
tdo_kprintf_swi_enable(int enable_)
{
  s_kprintf_swi = enable_;
}

static void
dbg_puts(const char *s_)
{
  while(*s_)
    dbg_putc(*s_++);
}

size_t
tdo_debug_read(char   *buf_,
               size_t  max_)
{
  size_t n = 0;
  while((n < max_) && (s_dbg_tail != s_dbg_head))
    {
      buf_[n++] = s_dbg[s_dbg_tail];
      s_dbg_tail = (s_dbg_tail + 1) % DEBUG_RING_SIZE;
    }
  return n;
}

static uint32_t
peek32(uint32_t addr_)
{
  int ok;
  uint32_t v = 0;
  int i;
  for(i = 0; i < 4; i++)
    v = (v << 8) | (opera_arm_harness_peek8(addr_ + i, &ok) & 0xFF);
  return v;
}

/*
  Minimal printf formatter that pulls arguments from the guest using the
  ARM APCS: first args in r1-r3 (r0 is the format), the rest on the stack.
*/
typedef struct
{
  int      idx;
  uint32_t sp;
} guest_args_t;

static uint32_t
next_arg(guest_args_t *a_)
{
  uint32_t v;
  if(a_->idx < 3)
    v = opera_arm_harness_reg(1 + a_->idx);
  else
    v = peek32(a_->sp + ((a_->idx - 3) * 4));
  a_->idx++;
  return v;
}

static void
guest_kprintf(void)
{
  guest_args_t args;
  uint32_t fmt = opera_arm_harness_reg(0);
  char out[1024];
  size_t o = 0;
  int guard = 0;

  args.idx = 0;
  args.sp  = opera_arm_harness_reg(13);

#define EMIT(c) do { if(o < sizeof(out) - 1) out[o++] = (c); } while(0)

  for(;;)
    {
      int ok;
      char spec[32];
      size_t si;
      char c = (char)opera_arm_harness_peek8(fmt++, &ok);

      if(!ok || c == 0 || ++guard > 4096)
        break;
      if(c != '%')
        {
          EMIT(c);
          continue;
        }

      /* collect a conversion spec: flags, width, precision, length */
      si = 0;
      spec[si++] = '%';
      for(;;)
        {
          c = (char)opera_arm_harness_peek8(fmt++, &ok);
          if(!ok || c == 0)
            goto done;
          if(strchr("-+ #0123456789.", c) && si < sizeof(spec) - 3)
            {
              spec[si++] = c;
              continue;
            }
          if(c == 'l' || c == 'h')
            continue; /* 32-bit guest: ignore length modifiers */
          break;
        }

      {
        char tmp[300];
        tmp[0] = 0;
        switch(c)
          {
          case 'd': case 'i':
            spec[si++] = 'd'; spec[si] = 0;
            snprintf(tmp, sizeof(tmp), spec, (int32_t)next_arg(&args));
            break;
          case 'u': case 'x': case 'X': case 'o':
            spec[si++] = c; spec[si] = 0;
            snprintf(tmp, sizeof(tmp), spec, (uint32_t)next_arg(&args));
            break;
          case 'p':
            snprintf(tmp, sizeof(tmp), "0x%08x", (uint32_t)next_arg(&args));
            break;
          case 'c':
            tmp[0] = (char)next_arg(&args); tmp[1] = 0;
            break;
          case 's':
            {
              char str[257];
              uint32_t p = next_arg(&args);
              int k;
              for(k = 0; k < 256; k++)
                {
                  str[k] = (char)opera_arm_harness_peek8(p + k, &ok);
                  if(!ok || !str[k])
                    break;
                }
              str[k] = 0;
              spec[si++] = 's'; spec[si] = 0;
              snprintf(tmp, sizeof(tmp), spec, str);
            }
            break;
          case '%':
            tmp[0] = '%'; tmp[1] = 0;
            break;
          default:
            /* unknown conversion, e.g. %f (no FPU): echo it */
            spec[si++] = c; spec[si] = 0;
            snprintf(tmp, sizeof(tmp), "%s", spec);
            break;
          }
        {
          char *t = tmp;
          while(*t)
            EMIT(*t++);
        }
      }
    }
 done:
  out[o] = 0;
  dbg_puts(out);
#undef EMIT
}

void
tdo_harness_on_swi(uint32_t swi_)
{
  swi_ &= 0x00FFFFFF;

  if(s_kprintf_swi && (swi_ == SWI_KPRINTF))
    guest_kprintf();

  if(s_swi_enabled)
    {
      uint32_t *e = &s_swi[s_swi_head * SWI_ENTRY_WORDS];
      e[0] = (uint32_t)s_frame;
      e[1] = swi_;
      e[2] = opera_arm_harness_reg(15) - 4;
      e[3] = opera_arm_harness_reg(0);
      e[4] = opera_arm_harness_reg(1);
      e[5] = opera_arm_harness_reg(2);
      e[6] = opera_arm_harness_reg(3);
      s_swi_head = (s_swi_head + 1) % SWI_RING_ENTRIES;
      if(s_swi_count < SWI_RING_ENTRIES)
        s_swi_count++;
    }
}

static uint32_t
ring_drain(uint32_t       *ring_,
           uint32_t        ring_entries_,
           uint32_t        entry_words_,
           uint32_t        head_,
           uint32_t       *count_,
           uint32_t       *out_,
           uint32_t        max_)
{
  uint32_t n = (*count_ < max_) ? *count_ : max_;
  uint32_t start = (head_ + ring_entries_ - *count_) % ring_entries_;
  uint32_t i;

  for(i = 0; i < n; i++)
    memcpy(&out_[i * entry_words_],
           &ring_[((start + i) % ring_entries_) * entry_words_],
           entry_words_ * sizeof(uint32_t));
  *count_ -= n;
  return n;
}

void
tdo_swi_trace_enable(int enable_)
{
  s_swi_enabled = enable_;
  s_swi_count = 0;
}

uint32_t
tdo_swi_trace_read(uint32_t *out_,
                   uint32_t  max_)
{
  return ring_drain(s_swi, SWI_RING_ENTRIES, SWI_ENTRY_WORDS,
                    s_swi_head, &s_swi_count, out_, max_);
}

static void
record_tracepoint(int i_, uint32_t pc_)
{
  uint32_t *e = &s_tr[s_tr_head * TRACE_ENTRY_WORDS];
  s_tp_hits[i_]++;
  e[0] = (uint32_t)s_frame;
  e[1] = pc_;
  e[2] = opera_arm_harness_reg(0);
  e[3] = opera_arm_harness_reg(1);
  e[4] = opera_arm_harness_reg(2);
  e[5] = opera_arm_harness_reg(3);
  e[6] = opera_arm_harness_reg(13);
  e[7] = opera_arm_harness_reg(14);
  e[8] = opera_arm_harness_cpsr();
  e[9] = s_tp_hits[i_];
  s_tr_head = (s_tr_head + 1) % TRACE_RING_ENTRIES;
  if(s_tr_count < TRACE_RING_ENTRIES)
    s_tr_count++;
}

static int
stop(int reason_, uint32_t pc_)
{
  s_halted  = 1;
  s_stop[0] = (uint32_t)reason_;
  s_stop[1] = pc_;
  s_stop[2] = (reason_ == TDO_STOP_WATCH) ? s_watch_addr : 0;
  s_stop[3] = (reason_ == TDO_STOP_WATCH) ? (uint32_t)s_watch_type : 0;
  update_hooks();
  return 1;
}

/* Called before every instruction while any hook is active.
   Returns 1 to stop the CPU before executing the instruction at pc_. */
int
tdo_harness_before_exec(uint32_t pc_)
{
  int i;

  if(s_halted)
    return 1;

  if(s_step == 1)
    {
      s_step = 2;          /* let exactly this instruction run */
      s_fetch_pc = pc_;
      s_skip_valid = 0;
      goto trace;
    }
  if(s_watch_hit)
    {
      s_watch_hit = 0;
      s_step = 0;
      return stop(TDO_STOP_WATCH, pc_);
    }
  if(s_step == 2)
    {
      s_step = 0;
      return stop(TDO_STOP_STEP, pc_);
    }
  if(s_halt_req)
    {
      s_halt_req = 0;
      return stop(TDO_STOP_HALT, pc_);
    }

  for(i = 0; i < s_bp_n; i++)
    {
      if(s_bp[i] == pc_)
        {
          if(s_skip_valid && s_skip_pc == pc_)
            break;
          s_skip_valid = 0;
          return stop(TDO_STOP_BREAK, pc_);
        }
    }
  s_skip_valid = 0;
  s_fetch_pc = pc_;

 trace:
  for(i = 0; i < s_tp_n; i++)
    if(s_tp_addr[i] == pc_)
      {
        record_tracepoint(i, pc_);
        break;
      }
  return 0;
}

void
tdo_harness_on_access(uint32_t addr_, uint32_t size_, int write_)
{
  int i;

  if(!write_ && ((addr_ & ~3u) == s_fetch_pc))
    return; /* instruction fetch, not a data read */

  for(i = 0; i < s_wp_n; i++)
    {
      int t = s_wp[i].type;
      if(write_ ? !(t & TDO_WATCH_WRITE) : !(t & TDO_WATCH_READ))
        continue;
      if((addr_ < s_wp[i].addr + s_wp[i].len) && (addr_ + size_ > s_wp[i].addr))
        {
          s_watch_hit  = 1;
          s_watch_addr = addr_;
          s_watch_type = write_ ? TDO_WATCH_WRITE : TDO_WATCH_READ;
          if(t == TDO_WATCH_ACCESS)
            s_watch_type = TDO_WATCH_ACCESS;
          g_tdo_exec_hooks = 1;
          return;
        }
    }
}

static void
update_hooks(void)
{
  g_tdo_watch_active = (s_wp_n > 0);
  g_tdo_exec_hooks = (s_halted || s_halt_req || s_step || s_watch_hit ||
                      s_bp_n > 0 || s_tp_n > 0);
}

void
tdo_harness_on_reset(void)
{
  s_halted = s_halt_req = s_step = s_watch_hit = s_skip_valid = 0;
  update_hooks();
}

void     tdo_dbg_halt(void)     { s_halt_req = 1; update_hooks(); }
int      tdo_dbg_halted(void)   { return s_halted; }

void
tdo_dbg_continue(void)
{
  if(s_halted)
    {
      s_skip_valid = 1;
      s_skip_pc    = opera_arm_harness_reg(15);
    }
  s_halted = 0;
  s_halt_req = 0;
  update_hooks();
}

void
tdo_dbg_step(void)
{
  s_halted = 0;
  s_halt_req = 0;
  s_step = 1;
  update_hooks();
}

void
tdo_dbg_stop_info(uint32_t *out_)
{
  memcpy(out_, s_stop, sizeof(s_stop));
}

int
tdo_bp_add(uint32_t addr_)
{
  int i;
  for(i = 0; i < s_bp_n; i++)
    if(s_bp[i] == addr_)
      return s_bp_n;
  if(s_bp_n >= BP_MAX)
    return -1;
  s_bp[s_bp_n++] = addr_;
  update_hooks();
  return s_bp_n;
}

int
tdo_bp_remove(uint32_t addr_)
{
  int i;
  for(i = 0; i < s_bp_n; i++)
    if(s_bp[i] == addr_)
      {
        s_bp[i] = s_bp[--s_bp_n];
        update_hooks();
        return 0;
      }
  return -1;
}

void tdo_bp_clear(void) { s_bp_n = 0; update_hooks(); }

int
tdo_wp_add(uint32_t addr_, uint32_t len_, int type_)
{
  if(s_wp_n >= WP_MAX || len_ == 0 || type_ < 1 || type_ > 3)
    return -1;
  s_wp[s_wp_n].addr = addr_;
  s_wp[s_wp_n].len  = len_;
  s_wp[s_wp_n].type = type_;
  s_wp_n++;
  update_hooks();
  return s_wp_n;
}

int
tdo_wp_remove(uint32_t addr_, uint32_t len_, int type_)
{
  int i;
  for(i = 0; i < s_wp_n; i++)
    if(s_wp[i].addr == addr_ && s_wp[i].len == len_ && s_wp[i].type == type_)
      {
        s_wp[i] = s_wp[--s_wp_n];
        update_hooks();
        return 0;
      }
  return -1;
}

void tdo_wp_clear(void) { s_wp_n = 0; update_hooks(); }

void
tdo_set_reg(int n_, uint32_t v_)
{
  if(n_ == 16)
    opera_arm_harness_set_cpsr(v_);
  else if(n_ >= 0 && n_ < 16)
    opera_arm_harness_set_reg(n_, v_);
}

int
tdo_trace_add(uint32_t addr_)
{
  if(s_tp_n >= TRACE_MAX_POINTS)
    return -1;
  s_tp_addr[s_tp_n] = addr_;
  s_tp_hits[s_tp_n] = 0;
  s_tp_n++;
  update_hooks();
  return s_tp_n;
}

int
tdo_trace_remove(uint32_t addr_)
{
  int i;
  for(i = 0; i < s_tp_n; i++)
    {
      if(s_tp_addr[i] == addr_)
        {
          s_tp_n--;
          s_tp_addr[i] = s_tp_addr[s_tp_n];
          s_tp_hits[i] = s_tp_hits[s_tp_n];
          update_hooks();
          return 0;
        }
    }
  return -1;
}

void
tdo_trace_clear(void)
{
  s_tp_n = 0;
  s_tr_count = 0;
  update_hooks();
}

uint32_t
tdo_trace_read(uint32_t *out_,
               uint32_t  max_)
{
  return ring_drain(s_tr, TRACE_RING_ENTRIES, TRACE_ENTRY_WORDS,
                    s_tr_head, &s_tr_count, out_, max_);
}

void
tdo_get_regs(uint32_t *out_)
{
  int i;
  for(i = 0; i < 16; i++)
    out_[i] = opera_arm_harness_reg(i);
  out_[16] = opera_arm_harness_cpsr();
}

uint32_t
tdo_mem_read(uint32_t  addr_,
             uint8_t  *out_,
             uint32_t  len_)
{
  uint32_t i;
  for(i = 0; i < len_; i++)
    {
      int ok;
      uint32_t v = opera_arm_harness_peek8(addr_ + i, &ok);
      if(!ok)
        break;
      out_[i] = (uint8_t)v;
    }
  return i;
}

uint32_t
tdo_mem_write(uint32_t       addr_,
              const uint8_t *in_,
              uint32_t       len_)
{
  uint32_t i;
  for(i = 0; i < len_; i++)
    if(!opera_arm_harness_poke8(addr_ + i, in_[i]))
      break;
  return i;
}
