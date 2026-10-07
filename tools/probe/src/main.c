/*
  probe - prints Portfolio 2.5 structure offsets and live kernel pointers.
  Used to derive the constants in tools/tdo/src/tdo/kernel.py:
      bin/3do-make -C tools/probe && .venv/bin/tdo log tools/probe/build/probe.iso --frames 400
*/
#include "device.h"
#include "driver.h"
#include "folio.h"
#include "graphics.h"
#include "io.h"
#include "mem.h"
#include "msgport.h"
#include "semaphore.h"
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
  tdo_log("PROBE: ItemNode=%d Node=%d ItemEntry=%d kb_MemFreeLists=%d kb_MemHdrList=%d kb_Drivers=%d kb_Semaphores=%d\n",
          (int)sizeof(ItemNode), (int)sizeof(Node), (int)sizeof(ItemEntry),
          OFF(struct KernelBase,kb_MemFreeLists), OFF(struct KernelBase,kb_MemHdrList),
          OFF(struct KernelBase,kb_Drivers), OFF(struct KernelBase,kb_Semaphores));
  tdo_log("PROBE: MsgPort.mp_Signal=%d mp_Msgs=%d mp_UserData=%d Message.msg_ReplyPort=%d msg_DataPtr=%d msg_DataSize=%d msg_MsgPort=%d\n",
          OFF(MsgPort,mp_Signal), OFF(MsgPort,mp_Msgs), OFF(MsgPort,mp_UserData),
          OFF(Message,msg_ReplyPort), OFF(Message,msg_DataPtr), OFF(Message,msg_DataSize), OFF(Message,msg_MsgPort));
  tdo_log("PROBE: Semaphore.sem_bit=%d sem_Owner=%d sem_NestCnt=%d sem_TaskWaitingList=%d\n",
          OFF(Semaphore,sem_bit), OFF(Semaphore,sem_Owner), OFF(Semaphore,sem_NestCnt), OFF(Semaphore,sem_TaskWaitingList));
  tdo_log("PROBE: MemHdr.memh_Types=%d memh_PageSize=%d memh_FreePageBits=%d memh_MemBase=%d memh_MemTop=%d memh_FreePageBitsSize=%d memh_PageShift=%d\n",
          OFF(MemHdr,memh_Types), OFF(MemHdr,memh_PageSize), OFF(MemHdr,memh_FreePageBits),
          OFF(MemHdr,memh_MemBase), OFF(MemHdr,memh_MemTop), OFF(MemHdr,memh_FreePageBitsSize), OFF(MemHdr,memh_PageShift));
  tdo_log("PROBE: MemList.meml_Types=%d meml_OwnBits=%d meml_MemHdr=%d meml_OwnBitsSize=%d\n",
          OFF(MemList,meml_Types), OFF(MemList,meml_OwnBits), OFF(MemList,meml_MemHdr), OFF(MemList,meml_OwnBitsSize));
  tdo_log("PROBE: Device.dev_Driver=%d dev_OpenCnt=%d dev_MaxUnitNum=%d Driver.drv_OpenCnt=%d Folio.f_OpenCount=%d f_MaxSwiFunctions=%d\n",
          OFF(Device,dev_Driver), OFF(Device,dev_OpenCnt), OFF(Device,dev_MaxUnitNum), OFF(Driver,drv_OpenCnt),
          OFF(Folio,f_OpenCount), OFF(Folio,f_MaxSwiFunctions));
  tdo_log("PROBE: IOReq.io_Dev=%d io_Info=%d io_Actual=%d io_Flags=%d io_Error=%d io_MsgItem=%d IOInfo.ioi_Command=%d ioi_Flags=%d ioi_Unit=%d ioi_Offset=%d\n",
          OFF(IOReq,io_Dev), OFF(IOReq,io_Info), OFF(IOReq,io_Actual), OFF(IOReq,io_Flags), OFF(IOReq,io_Error),
          OFF(IOReq,io_MsgItem), OFF(IOInfo,ioi_Command), OFF(IOInfo,ioi_Flags), OFF(IOInfo,ioi_Unit), OFF(IOInfo,ioi_Offset));
  tdo_log("PROBE: Bitmap.bm_Buffer=%d bm_Width=%d bm_Height=%d Screen.scr_ScreenGroupPtr=%d scr_VDLItem=%d scr_BitmapCount=%d scr_BitmapList=%d\n",
          OFF(Bitmap,bm_Buffer), OFF(Bitmap,bm_Width), OFF(Bitmap,bm_Height),
          OFF(Screen,scr_ScreenGroupPtr), OFF(Screen,scr_VDLItem), OFF(Screen,scr_BitmapCount), OFF(Screen,scr_BitmapList));
  tdo_log("PROBE: done\n");
  for(;;) Yield();
  return 0;
}
