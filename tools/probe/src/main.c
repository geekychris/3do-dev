/*
  probe - prints Portfolio 2.5 structure offsets and live kernel pointers.
  Used to derive the constants in tools/tdo/src/tdo/kernel.py:
      bin/3do-make -C tools/probe && .venv/bin/tdo log tools/probe/build/probe.iso --frames 400
*/
#include "folio.h"
#include "kernel.h"
#include "list.h"
#include "task.h"
#include "types.h"
#include "tdo.h"

#define OFF(t,f) ((int)Offset(t*, f))

int
main(void)
{
  tdo_log("PROBE: KernelBase=%p &KernelBase=%p current=%p main=%p\n",
          KernelBase, &KernelBase, KernelBase->kb_CurrentTask, main);
  tdo_log("PROBE: Folio=%d kb_FolioList=%d kb_TaskWaitQ=%d kb_TaskReadyQ=%d kb_CurrentTask=%d\n",
          (int)sizeof(Folio), OFF(struct KernelBase,kb_FolioList), OFF(struct KernelBase,kb_TaskWaitQ),
          OFF(struct KernelBase,kb_TaskReadyQ), OFF(struct KernelBase,kb_CurrentTask));
  tdo_log("PROBE: kb_ItemTable=%d kb_MaxItem=%d kb_NumTaskSwitches=%d kb_Tasks=%d kb_MemEnd=%d kb_Devices=%d kb_MsgPorts=%d\n",
          OFF(struct KernelBase,kb_ItemTable), OFF(struct KernelBase,kb_MaxItem),
          OFF(struct KernelBase,kb_NumTaskSwitches), OFF(struct KernelBase,kb_Tasks),
          OFF(struct KernelBase,kb_MemEnd), OFF(struct KernelBase,kb_Devices), OFF(struct KernelBase,kb_MsgPorts));
  tdo_log("PROBE: n_Name=%d n_Priority=%d n_Item=%d n_Owner=%d n_Type=%d n_Flags=%d\n",
          OFF(ItemNode,n_Name), OFF(ItemNode,n_Priority), OFF(ItemNode,n_Item), OFF(ItemNode,n_Owner),
          OFF(ItemNode,n_Type), OFF(ItemNode,n_Flags));
  tdo_log("PROBE: t_ThreadTask=%d t_WaitBits=%d t_SigBits=%d t_StackBase=%d t_StackSize=%d t_Private1=%d\n",
          OFF(Task,t_ThreadTask), OFF(Task,t_WaitBits), OFF(Task,t_SigBits),
          OFF(Task,t_StackBase), OFF(Task,t_StackSize), OFF(Task,t_Private1));
  tdo_log("PROBE: t_SuperStackBase=%d t_WaitItem=%d t_FreeMemoryLists=%d t_ElapsedTime=%d t_NumTaskLaunch=%d t_Flags=%d t_TasksLinkNode=%d sizeof=%d\n",
          OFF(Task,t_SuperStackBase), OFF(Task,t_WaitItem), OFF(Task,t_FreeMemoryLists),
          OFF(Task,t_ElapsedTime), OFF(Task,t_NumTaskLaunch), OFF(Task,t_Flags),
          OFF(Task,t_TasksLinkNode), (int)sizeof(Task));
  tdo_log("PROBE: List.ListAnchor=%d sizeof(List)=%d\n", OFF(List,ListAnchor), (int)sizeof(List));
  tdo_log("PROBE: done\n");
  for(;;) Yield();
  return 0;
}
