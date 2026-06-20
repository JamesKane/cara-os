// SPDX-License-Identifier: BSD-2-Clause
//
// Syscall dispatcher. Trap entry has already saved a full frame on the
// dying user task's kstack and S-mode is running with SUM=1, so user
// pointers handed in via a1/a2/... are accessible directly.
//
// Phase 3+ (LVO.md §5.2): every exec.library `syscall` flavour LVO
// reaches here via its trampoline (Cara_Trampoline_<Name> in
// src/croi/exec_lib/trampolines.S). The trampoline preserves a0..a5
// from the user-mode caller, sets a7 = SYS_<Name>, and ecalls. Each
// arm below reads the args it cares about and routes to the matching
// Croi_*_Impl helper from src/croi/exec_lib/.

#include <cara/dos_lib.h>
#include <cara/exec_lib.h>
#include <cara/graphics_lib.h>
#include <cara/intuition_lib.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/sysno.h>
#include <cara/trap.h>
#include <cara/types.h>
#include <cara/utility_lib.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <exec/types.h>

static volatile bool g_user_exited = false;
static volatile i64 g_user_exit_status = 0;

bool Croi_Syscall_UserExited(void)
{
    return g_user_exited;
}
i64 Croi_Syscall_UserExitStatus(void)
{
    return g_user_exit_status;
}
void Croi_Syscall_ResetUserExit(void)
{
    g_user_exited = false;
    g_user_exit_status = 0;
}

// Bounded copy from user-VA to a kernel buffer. Caller passes max so
// we never run forever on a malformed pointer.
static u32 copy_user_str(char *dst, u32 dst_cap, const char *user_src, u32 user_max)
{
    if (dst_cap == 0) {
        return 0;
    }
    u32 i = 0;
    u32 cap = dst_cap - 1;
    while (i < cap && i < user_max && user_src[i] != 0) {
        dst[i] = user_src[i];
        i++;
    }
    dst[i] = 0;
    return i;
}

static i64 sys_log_write(i64 level, const char *user_tag, const char *user_msg, i64 msg_len)
{
    if (msg_len < 0 || msg_len > (i64)CARA_LOG_MSG_LEN) {
        return -CARA_EINVAL;
    }
    char tag[CARA_LOG_TAG_LEN + 1] = { 0 };
    (void)copy_user_str(tag, sizeof(tag), user_tag, CARA_LOG_TAG_LEN);

    char msg[CARA_LOG_MSG_LEN];
    u32 n = (u32)msg_len < CARA_LOG_MSG_LEN - 1 ? (u32)msg_len : CARA_LOG_MSG_LEN - 1;
    for (u32 i = 0; i < n; i++) {
        msg[i] = user_msg[i];
    }
    msg[n] = 0;

    Croi_Log((LogLevel)level, tag, "%s", msg);
    return 0;
}

[[noreturn]] static void sys_exit(i64 status)
{
    g_user_exited = true;
    g_user_exit_status = status;
    LOG_INFO("user", "exit status=%lld", status);
    Croi_TaskExit();
}

i64 Croi_Syscall_Dispatch(struct TrapFrame *frame)
{
    i64 num = (i64)frame->x[17]; // a7
    i64 a0 = (i64)frame->x[10];
    i64 a1 = (i64)frame->x[11];
    i64 a2 = (i64)frame->x[12];
    i64 a3 = (i64)frame->x[13];

    switch (num) {
    case SYS_LOG_WRITE:
        return sys_log_write(a0, (const char *)(uptr)a1, (const char *)(uptr)a2, a3);
    case SYS_EXIT:
        sys_exit(a0);
        // unreachable

    // ---- exec.library library lifecycle ----
    case SYS_OpenLibrary:
        return (i64)(uptr)Croi_OpenLibrary_Impl((STRPTR)(uptr)a0, (ULONG)a1);
    case SYS_OldOpenLibrary:
        return (i64)(uptr)Croi_OldOpenLibrary_Impl((STRPTR)(uptr)a0);
    case SYS_CloseLibrary:
        Croi_CloseLibrary_Impl((struct Library *)(uptr)a0);
        return 0;

    // ---- exec.library memory ----
    case SYS_AllocMem:
        return (i64)(uptr)Croi_AllocMem_Impl((ULONG)a0, (ULONG)a1);
    case SYS_FreeMem:
        Croi_FreeMem_Impl((APTR)(uptr)a0, (ULONG)a1);
        return 0;
    case SYS_AllocVec:
        return (i64)(uptr)Croi_AllocVec_Impl((ULONG)a0, (ULONG)a1);
    case SYS_FreeVec:
        Croi_FreeVec_Impl((APTR)(uptr)a0);
        return 0;

    // ---- exec.library signals ----
    case SYS_Wait:
        return (i64)Croi_Wait_Impl((ULONG)a0);
    case SYS_Signal:
        Croi_Signal_Impl((struct Task *)(uptr)a0, (ULONG)a1);
        return 0;
    case SYS_AllocSignal:
        return (i64)Croi_AllocSignal_Impl((LONG)a0);
    case SYS_FreeSignal:
        Croi_FreeSignal_Impl((LONG)a0);
        return 0;
    case SYS_SetSignal:
        return (i64)Croi_SetSignal_Impl((ULONG)a0, (ULONG)a1);
    case SYS_FindTask:
        return (i64)(uptr)Croi_FindTask_Impl((STRPTR)(uptr)a0);

    // ---- dos.library (syscall-flavour rows) ----
    case SYS_Dos_IoErr:
        return (i64)Croi_Dos_IoErr_Impl();
    case SYS_Dos_HandlerPort:
        return (i64)(uptr)Croi_Dos_HandlerPort_Impl();
    case SYS_Dos_Lock:
        return (i64)(uptr)Croi_Dos_Lock_Impl((STRPTR)(uptr)a0, (LONG)a1);
    case SYS_Dos_UnLock:
        Croi_Dos_UnLock_Impl((BPTR)(uptr)a0);
        return 0;
    case SYS_Dos_DupLock:
        return (i64)(uptr)Croi_Dos_DupLock_Impl((BPTR)(uptr)a0);
    case SYS_Dos_CurrentDir:
        return (i64)(uptr)Croi_Dos_CurrentDir_Impl((BPTR)(uptr)a0);
    case SYS_Dos_Examine:
        return (i64)Croi_Dos_Examine_Impl((BPTR)(uptr)a0, (struct FileInfoBlock *)(uptr)a1);
    case SYS_Dos_ExNext:
        return (i64)Croi_Dos_ExNext_Impl((BPTR)(uptr)a0, (struct FileInfoBlock *)(uptr)a1);
    case SYS_Dos_Open:
        return (i64)(uptr)Croi_Dos_Open_Impl((STRPTR)(uptr)a0, (LONG)a1);
    case SYS_Dos_Close:
        return (i64)Croi_Dos_Close_Impl((BPTR)(uptr)a0);
    case SYS_Dos_Read:
        return (i64)Croi_Dos_Read_Impl((BPTR)(uptr)a0, (APTR)(uptr)a1, (LONG)a2);
    case SYS_Dos_Write:
        return (i64)Croi_Dos_Write_Impl((BPTR)(uptr)a0, (APTR)(uptr)a1, (LONG)a2);
    case SYS_Dos_Seek:
        return (i64)Croi_Dos_Seek_Impl((BPTR)(uptr)a0, (LONG)a1, (LONG)a2);
    case SYS_Dos_DeleteFile:
        return (i64)Croi_Dos_DeleteFile_Impl((STRPTR)(uptr)a0);
    case SYS_Dos_Rename:
        return (i64)Croi_Dos_Rename_Impl((STRPTR)(uptr)a0, (STRPTR)(uptr)a1);
    case SYS_Dos_Info:
        return (i64)Croi_Dos_Info_Impl((BPTR)(uptr)a0, (struct InfoData *)(uptr)a1);
    case SYS_Dos_CreateDir:
        return (i64)(uptr)Croi_Dos_CreateDir_Impl((STRPTR)(uptr)a0);
    case SYS_Dos_Output:
        return (i64)(uptr)Croi_Dos_Output_Impl();
    case SYS_Dos_Input:
        return (i64)(uptr)Croi_Dos_Input_Impl();
    case SYS_Dos_Delay:
        Croi_Dos_Delay_Impl((LONG)a0);
        return 0;

    // ---- graphics.library (Dath rasteriser) ----
    case SYS_Gfx_AllocBitMap:
        return (i64)(uptr)Croi_Gfx_AllocBitMap_Impl((ULONG)a0, (ULONG)a1, (ULONG)a2, (ULONG)a3,
                                                    (const struct BitMap *)(uptr)frame->x[14]);
    case SYS_Gfx_FreeBitMap:
        Croi_Gfx_FreeBitMap_Impl((struct BitMap *)(uptr)a0);
        return 0;
    case SYS_Gfx_InitRastPort:
        Croi_Gfx_InitRastPort_Impl((struct RastPort *)(uptr)a0);
        return 0;
    case SYS_Gfx_SetRast:
        Croi_Gfx_SetRast_Impl((struct RastPort *)(uptr)a0, (ULONG)a1);
        return 0;
    case SYS_Gfx_Move:
        Croi_Gfx_Move_Impl((struct RastPort *)(uptr)a0, (WORD)a1, (WORD)a2);
        return 0;
    case SYS_Gfx_Draw:
        Croi_Gfx_Draw_Impl((struct RastPort *)(uptr)a0, (WORD)a1, (WORD)a2);
        return 0;
    case SYS_Gfx_RectFill:
        Croi_Gfx_RectFill_Impl((struct RastPort *)(uptr)a0, (WORD)a1, (WORD)a2, (WORD)a3,
                               (WORD)frame->x[14]);
        return 0;
    case SYS_Gfx_ReadPixel:
        return (i64)Croi_Gfx_ReadPixel_Impl((struct RastPort *)(uptr)a0, (WORD)a1, (WORD)a2);
    case SYS_Gfx_WritePixel:
        return (i64)Croi_Gfx_WritePixel_Impl((struct RastPort *)(uptr)a0, (WORD)a1, (WORD)a2);
    case SYS_Gfx_SetAPen:
        Croi_Gfx_SetAPen_Impl((struct RastPort *)(uptr)a0, (ULONG)a1);
        return 0;
    case SYS_Gfx_SetBPen:
        Croi_Gfx_SetBPen_Impl((struct RastPort *)(uptr)a0, (ULONG)a1);
        return 0;
    case SYS_Gfx_SetDrMd:
        Croi_Gfx_SetDrMd_Impl((struct RastPort *)(uptr)a0, (ULONG)a1);
        return 0;
    case SYS_Gfx_Blt:
        return (i64)Croi_Gfx_Blt_Impl((const struct GfxBltArgs *)(uptr)a0);
    case SYS_Gfx_TextLength:
        return (i64)Croi_Gfx_TextLength_Impl((struct RastPort *)(uptr)a0, (STRPTR)(uptr)a1,
                                             (ULONG)a2);
    case SYS_Gfx_Text:
        Croi_Gfx_Text_Impl((struct RastPort *)(uptr)a0, (STRPTR)(uptr)a1, (ULONG)a2);
        return 0;
    case SYS_Gfx_SetFont:
        Croi_Gfx_SetFont_Impl((struct RastPort *)(uptr)a0, (struct TextFont *)(uptr)a1);
        return 0;
    case SYS_Gfx_OpenFont:
        return (i64)(uptr)Croi_Gfx_OpenFont_Impl((struct TextAttr *)(uptr)a0);
    case SYS_Gfx_CloseFont:
        Croi_Gfx_CloseFont_Impl((struct TextFont *)(uptr)a0);
        return 0;
    case SYS_Gfx_InitArea:
        Croi_Gfx_InitArea_Impl((struct AreaInfo *)(uptr)a0, (APTR)(uptr)a1, (WORD)a2);
        return 0;
    case SYS_Gfx_AreaMove:
        return (i64)Croi_Gfx_AreaMove_Impl((struct RastPort *)(uptr)a0, (WORD)a1, (WORD)a2);
    case SYS_Gfx_AreaDraw:
        return (i64)Croi_Gfx_AreaDraw_Impl((struct RastPort *)(uptr)a0, (WORD)a1, (WORD)a2);
    case SYS_Gfx_AreaEnd:
        return (i64)Croi_Gfx_AreaEnd_Impl((struct RastPort *)(uptr)a0);

    // ---- utility.library allocating tag helpers ----
    case SYS_AllocateTagItems:
        return (i64)(uptr)Croi_Utility_AllocateTagItems_Impl((ULONG)a0);
    case SYS_CloneTagItems:
        return (i64)(uptr)Croi_Utility_CloneTagItems_Impl((struct TagItem *)(uptr)a0);
    case SYS_FreeTagItems:
        Croi_Utility_FreeTagItems_Impl((struct TagItem *)(uptr)a0);
        return 0;
    case SYS_RefreshTagItemClones:
        Croi_Utility_RefreshTagItemClones_Impl((struct TagItem *)(uptr)a0,
                                               (struct TagItem *)(uptr)a1);
        return 0;

    // ---- exec.library task-switch control ----
    case SYS_Forbid:
        Croi_Forbid_Impl();
        return 0;
    case SYS_Permit:
        Croi_Permit_Impl();
        return 0;
    case SYS_Disable:
        Croi_Disable_Impl();
        return 0;
    case SYS_Enable:
        Croi_Enable_Impl();
        return 0;

    // ---- exec.library semaphores ----
    case SYS_InitSemaphore:
        Croi_InitSemaphore_Impl((struct SignalSemaphore *)(uptr)a0);
        return 0;
    case SYS_ObtainSemaphore:
        Croi_ObtainSemaphore_Impl((struct SignalSemaphore *)(uptr)a0);
        return 0;
    case SYS_ReleaseSemaphore:
        Croi_ReleaseSemaphore_Impl((struct SignalSemaphore *)(uptr)a0);
        return 0;
    case SYS_AttemptSemaphore:
        return (i64)Croi_AttemptSemaphore_Impl((struct SignalSemaphore *)(uptr)a0);

    // ---- exec.library IPC ----
    case SYS_PutMsg:
        Croi_PutMsg_Impl((struct MsgPort *)(uptr)a0, (struct Message *)(uptr)a1);
        return 0;
    case SYS_GetMsg:
        return (i64)(uptr)Croi_GetMsg_Impl((struct MsgPort *)(uptr)a0);
    case SYS_WaitPort:
        return (i64)(uptr)Croi_WaitPort_Impl((struct MsgPort *)(uptr)a0);
    case SYS_ReplyMsg:
        Croi_ReplyMsg_Impl((struct Message *)(uptr)a0);
        return 0;
    case SYS_CreateMsgPort:
        return (i64)(uptr)Croi_CreateMsgPort_Impl();
    case SYS_DeleteMsgPort:
        Croi_DeleteMsgPort_Impl((struct MsgPort *)(uptr)a0);
        return 0;

    // ---- intuition.library windows ----
    case SYS_OpenWindow:
        return (i64)(uptr)Croi_OpenWindow_Impl((struct NewWindow *)(uptr)a0);
    case SYS_CloseWindow:
        Croi_CloseWindow_Impl((struct Window *)(uptr)a0);
        return 0;

    // ---- intuition.library gadgets ----
    case SYS_AddGadget:
        return (i64)Croi_AddGadget_Impl((struct Window *)(uptr)a0, (struct Gadget *)(uptr)a1,
                                        (ULONG)a2);
    case SYS_RemoveGadget:
        return (i64)Croi_RemoveGadget_Impl((struct Window *)(uptr)a0, (struct Gadget *)(uptr)a1);
    case SYS_ActivateGadget:
        return (i64)Croi_ActivateGadget_Impl((struct Gadget *)(uptr)a0, (struct Window *)(uptr)a1,
                                             (struct Requester *)(uptr)a2);

    default:
        LOG_WARN("sysc", "unknown syscall a7=%lld", num);
        return -CARA_ENOTFOUND;
    }
}
