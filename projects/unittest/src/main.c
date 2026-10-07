/*
  unittest - example of on-target tests.

  Runs checks on the (emulated) 3DO and reports through tdo_check(), which
  prints TDO:PASS / TDO:FAIL lines; tdo_test_done() prints the summary that
  `./3do test unittest` waits for and turns into an exit code.

  Use this pattern to test game logic against the real OS and compiler
  (Norcroft's integer/fixed-point behaviour, struct layout, file access...).
*/
#include "debug.h"
#include "filefunctions.h"
#include "item.h"
#include "kernel.h"
#include "mem.h"
#include "msgport.h"
#include "operamath.h"
#include "semaphore.h"
#include "stdio.h"
#include "string.h"
#include "types.h"

#include "tdo.h"

typedef struct
{
  u8  a;
  u32 b;
  u16 c;
} Packed;

static void
test_compiler(void)
{
  s32 neg = -7;
  u32 big = 0xFFFFFFFFu;

  tdo_check_eq("int/sizeof", (s32)sizeof(int), 4);
  tdo_check_eq("ptr/sizeof", (s32)sizeof(void*), 4);
  tdo_check_eq("struct/padding", (s32)sizeof(Packed), 12);
  tdo_check_eq("div/truncates-toward-zero", neg / 2, -3);
  tdo_check_eq("shift/arithmetic", neg >> 1, -4);
  tdo_check("u32/wraps", (u32)(big + 1u) == 0u);
  {
    u32 word = 0x11223344;
    tdo_check_eq("endian/big", *(u8*)&word, 0x11);
  }
}

static void
test_libc(void)
{
  char buf[64];

  sprintf(buf, "%d-%x-%s", 42, 255, "ok");
  tdo_check("sprintf", strcmp(buf, "42-ff-ok") == 0);
  tdo_check_eq("strlen", (s32)strlen("3do"), 3);
  memset(buf, 'z', 8);
  buf[8] = 0;
  tdo_check("memset", strcmp(buf, "zzzzzzzz") == 0);
}

static void
test_math(void)
{
  frac16 one = Convert32_F16(1);

  OpenMathFolio();
  tdo_check_eq("math/MulSF16", MulSF16(Convert32_F16(3), Convert32_F16(4)), Convert32_F16(12));
  tdo_check_eq("math/DivSF16", DivSF16(Convert32_F16(10), Convert32_F16(4)), Convert32_F16(5) >> 1);
  /* 256 units per circle: 64 = 90 degrees */
  tdo_check("math/SinF16(90deg)", SinF16(Convert32_F16(64)) >= one - 2);
  tdo_check("math/CosF16(0)", CosF16(0) >= one - 2);
  tdo_check_eq("math/SqrtF16", (s32)SqrtF16((ufrac16)Convert32_F16(16)), Convert32_F16(4));
}

static void
test_kernel(void)
{
  void *p;
  Item port;
  Item sem;

  p = AllocMem(4096, MEMTYPE_ANY);
  tdo_check("mem/AllocMem", p != NULL);
  if(p)
    FreeMem(p, 4096);

  port = CreateMsgPort("unittest.port", 0, 0);
  tdo_check("kernel/CreateMsgPort", port >= 0);
  if(port >= 0)
    tdo_check("kernel/DeleteMsgPort", DeleteMsgPort(port) >= 0);

  sem = CreateSemaphore("unittest.sem", 0);
  tdo_check("kernel/CreateSemaphore", sem >= 0);
  if(sem >= 0)
    {
      tdo_check("kernel/LockSemaphore", LockSemaphore(sem, 0) > 0);
      tdo_check("kernel/UnlockSemaphore", UnlockSemaphore(sem) >= 0);
      DeleteSemaphore(sem);
    }
}

static void
test_filesystem(void)
{
  Item f = OpenDiskFile("LaunchMe");
  tdo_check("fs/OpenDiskFile(LaunchMe)", f >= 0);
  if(f >= 0)
    CloseDiskFile(f);
  tdo_check("fs/missing-file-fails", OpenDiskFile("does/not/exist") < 0);
}

int
main(void)
{
  tdo_log("unittest: start\n");
  test_compiler();
  test_libc();
  test_math();
  test_kernel();
  test_filesystem();
  tdo_test_done();
  return 0;
}
