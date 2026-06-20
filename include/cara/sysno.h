// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS syscall numbers — a stable wire ABI between user-mode
// trampolines (in 0x4000_0000 library RX pages) and the Croi
// dispatcher (src/croi/syscall/syscall.c). Numbers are allocated by
// phase: Phase 1 (boot smoke) starts at 1, Phase 3 (exec.library)
// continues from 3, future phases append.
//
// User-mode never references these numbers directly — every call goes
// through a per-LVO trampoline (Cara_Trampoline_<Name> in
// src/croi/exec_lib/trampolines.S) that does `li a7, <number>; ecall;
// ret`. The numbers must stay stable so trampolines compiled into a
// library RX page keep working across kernel updates.

#ifndef CARA_SYSNO_H
#define CARA_SYSNO_H

#ifndef __ASSEMBLER__
enum {
    // Phase 1 — kernel smoke / log surface.
    SYS_LOG_WRITE = 1,
    SYS_EXIT = 2,

    // Phase 3 — exec.library.
    //
    // OpenLibrary and OldOpenLibrary have separate numbers so the
    // dispatcher can route OldOpenLibrary directly through the same impl
    // with version forced to 0, without needing a custom trampoline.
    SYS_OpenLibrary = 3,
    SYS_CloseLibrary = 4,
    SYS_OldOpenLibrary = 5,
    SYS_AllocMem = 6,
    SYS_FreeMem = 7,
    SYS_Wait = 8,
    SYS_Signal = 9,
    SYS_AllocSignal = 10,
    SYS_FreeSignal = 11,
    SYS_SetSignal = 12,
    SYS_PutMsg = 13,
    SYS_GetMsg = 14,
    SYS_WaitPort = 15,

    // Phase 3 — intuition.library. Each maps to a Croi_*_Impl in
    // src/croi/intuition_lib/ that bridges onto the Leargas_*
    // window-system substrate. Declaration order mirrors
    // tools/lvo-gen/intuition.conf.
    SYS_AddGadget = 16,
    SYS_CloseWindow = 17,
    SYS_OpenWindow = 18,
    SYS_RemoveGadget = 19,
    SYS_ActivateGadget = 20,

    // (21, 22 retired in L3.7: the Phase-2 CaraFS stopgap SYS_Fs_Read/
    // Write — Clar's drawer note now goes through dos.library. The numbers
    // stay reserved so the trampoline wire ABI is never reused.)

    // Phase 3 L1 — more exec.library (docs/PHASE3.md).
    SYS_AllocVec = 23,
    SYS_FreeVec = 24,
    SYS_CreateMsgPort = 25,
    SYS_DeleteMsgPort = 26,
    SYS_ReplyMsg = 27,
    SYS_FindTask = 28,
    SYS_InitSemaphore = 29,
    SYS_ObtainSemaphore = 30,
    SYS_ReleaseSemaphore = 31,
    SYS_AttemptSemaphore = 32,
    SYS_Forbid = 33,
    SYS_Permit = 34,
    SYS_Disable = 35,
    SYS_Enable = 36,

    // Phase 3 L2 — utility.library allocating tag helpers (the rest of
    // utility is `local` flavour and never traps).
    SYS_AllocateTagItems = 37,
    SYS_CloneTagItems = 38,
    SYS_FreeTagItems = 39,
    SYS_RefreshTagItemClones = 40,

    // Phase 3 L3 — dos.library (the non-packet, syscall-flavour rows).
    SYS_Dos_IoErr = 41,
    SYS_Dos_HandlerPort = 42, // fetch the dos handler's MsgPort (server path)
    SYS_Dos_Lock = 43,
    SYS_Dos_UnLock = 44,
    SYS_Dos_DupLock = 45,
    SYS_Dos_CurrentDir = 46,
    SYS_Dos_Examine = 47,
    SYS_Dos_ExNext = 48,
    SYS_Dos_Open = 49,
    SYS_Dos_Close = 50,
    SYS_Dos_Read = 51,
    SYS_Dos_Write = 52,
    SYS_Dos_Seek = 53,
    SYS_Dos_DeleteFile = 54,
    SYS_Dos_Rename = 55,
    SYS_Dos_Info = 56,
    SYS_Dos_CreateDir = 57,
    SYS_Dos_Output = 58, // Process pr_COS (lazily-created console handle)
    SYS_Dos_Input = 59,  // Process pr_CIS
    SYS_Dos_Delay = 60,  // spin-yield shim over Croi_Time

    // Phase 3 L4 — graphics.library (Dath rasteriser, syscall flavour).
    SYS_Gfx_AllocBitMap = 61,
    SYS_Gfx_FreeBitMap = 62,
    SYS_Gfx_InitRastPort = 63,
    SYS_Gfx_SetRast = 64,
    SYS_Gfx_Move = 65,
    SYS_Gfx_Draw = 66,
    SYS_Gfx_RectFill = 67,
    SYS_Gfx_ReadPixel = 68,
    SYS_Gfx_WritePixel = 69,
    SYS_Gfx_SetAPen = 70,
    SYS_Gfx_SetBPen = 71,
    SYS_Gfx_SetDrMd = 72,
    SYS_Gfx_Blt = 73, // shared by BltBitMap/BltBitMapRastPort/ClipBlit (GfxBltArgs)
};
#else
// Phase 1 — kernel smoke / log surface.
#define SYS_LOG_WRITE 1
#define SYS_EXIT 2

// Phase 3 — exec.library.
#define SYS_OpenLibrary 3
#define SYS_CloseLibrary 4
#define SYS_OldOpenLibrary 5
#define SYS_AllocMem 6
#define SYS_FreeMem 7
#define SYS_Wait 8
#define SYS_Signal 9
#define SYS_AllocSignal 10
#define SYS_FreeSignal 11
#define SYS_SetSignal 12
#define SYS_PutMsg 13
#define SYS_GetMsg 14
#define SYS_WaitPort 15

// Phase 3 — intuition.library.
#define SYS_AddGadget 16
#define SYS_CloseWindow 17
#define SYS_OpenWindow 18
#define SYS_RemoveGadget 19
#define SYS_ActivateGadget 20

// (21, 22 retired in L3.7 — Phase-2 CaraFS stopgap; numbers stay reserved.)

// Phase 3 L1 — more exec.library.
#define SYS_AllocVec 23
#define SYS_FreeVec 24
#define SYS_CreateMsgPort 25
#define SYS_DeleteMsgPort 26
#define SYS_ReplyMsg 27
#define SYS_FindTask 28
#define SYS_InitSemaphore 29
#define SYS_ObtainSemaphore 30
#define SYS_ReleaseSemaphore 31
#define SYS_AttemptSemaphore 32
#define SYS_Forbid 33
#define SYS_Permit 34
#define SYS_Disable 35
#define SYS_Enable 36

// Phase 3 L2 — utility.library allocating tag helpers.
#define SYS_AllocateTagItems 37
#define SYS_CloneTagItems 38
#define SYS_FreeTagItems 39
#define SYS_RefreshTagItemClones 40

// Phase 3 L3 — dos.library.
#define SYS_Dos_IoErr 41
#define SYS_Dos_HandlerPort 42
#define SYS_Dos_Lock 43
#define SYS_Dos_UnLock 44
#define SYS_Dos_DupLock 45
#define SYS_Dos_CurrentDir 46
#define SYS_Dos_Examine 47
#define SYS_Dos_ExNext 48
#define SYS_Dos_Open 49
#define SYS_Dos_Close 50
#define SYS_Dos_Read 51
#define SYS_Dos_Write 52
#define SYS_Dos_Seek 53
#define SYS_Dos_DeleteFile 54
#define SYS_Dos_Rename 55
#define SYS_Dos_Info 56
#define SYS_Dos_CreateDir 57
#define SYS_Dos_Output 58
#define SYS_Dos_Input 59
#define SYS_Dos_Delay 60

// Phase 3 L4 — graphics.library.
#define SYS_Gfx_AllocBitMap 61
#define SYS_Gfx_FreeBitMap 62
#define SYS_Gfx_InitRastPort 63
#define SYS_Gfx_SetRast 64
#define SYS_Gfx_Move 65
#define SYS_Gfx_Draw 66
#define SYS_Gfx_RectFill 67
#define SYS_Gfx_ReadPixel 68
#define SYS_Gfx_WritePixel 69
#define SYS_Gfx_SetAPen 70
#define SYS_Gfx_SetBPen 71
#define SYS_Gfx_SetDrMd 72
#define SYS_Gfx_Blt 73
#endif

#endif
