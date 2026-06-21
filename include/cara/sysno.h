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
    SYS_Gfx_TextLength = 74,
    SYS_Gfx_Text = 75,
    SYS_Gfx_SetFont = 76,
    SYS_Gfx_OpenFont = 77,
    SYS_Gfx_CloseFont = 78,
    SYS_Gfx_InitArea = 79,
    SYS_Gfx_AreaMove = 80,
    SYS_Gfx_AreaDraw = 81,
    SYS_Gfx_AreaEnd = 82,

    // Phase 3 L5 — more intuition.library (the original 5 are 16-20).
    SYS_OpenWindowTagList = 83,
    SYS_ModifyIDCMP = 84,
    SYS_MoveWindow = 85,
    SYS_SizeWindow = 86,
    SYS_WindowToFront = 87,
    SYS_WindowToBack = 88,
    SYS_SetWindowTitles = 89,
    SYS_ActivateWindow = 90,
    SYS_SetMenuStrip = 91,
    SYS_ClearMenuStrip = 92,
    SYS_ItemAddress = 93,
    SYS_CurrentTime = 94,
    SYS_DoubleClick = 95,
    SYS_DisplayBeep = 96,
    SYS_ReportMouse = 97,
    SYS_IntuiTextLength = 98,
    SYS_PrintIText = 99,
    SYS_DrawBorder = 100,
    SYS_AddGList = 101,
    SYS_RemoveGList = 102,
    SYS_OnGadget = 103,
    SYS_OffGadget = 104,
    SYS_RefreshGList = 105,
    SYS_RefreshWindowFrame = 106,
    SYS_AutoRequest = 107, // local stub packs AutoReqArgs (8-arg LVO)
    SYS_EasyRequestArgs = 108,
    SYS_OpenScreen = 109,
    SYS_CloseScreen = 110,
    SYS_OpenScreenTagList = 111,

    // Phase 3 L6 — exec device IO primitives (device.c + the registry).
    SYS_OpenDevice = 112,
    SYS_CloseDevice = 113,
    SYS_DoIO = 114,
    SYS_SendIO = 115,
    SYS_CheckIO = 116,
    SYS_WaitIO = 117,
    SYS_AbortIO = 118,

    // Phase 3 L7 — BOOPSI public class registry (boopsi_registry.c).
    SYS_FindClass = 119,
    SYS_AddClass = 120,
    SYS_RemoveClass = 121,

    // Phase 3 L8 — gadtools.library render context (gadtools_lib.c).
    SYS_GT_GetVisualInfoA = 122,
    SYS_GT_FreeVisualInfo = 123,
    SYS_GT_CreateContext = 124,
    SYS_GT_FreeGadgets = 125,
    SYS_GT_CreateGadgetA = 126,
    SYS_GT_SetGadgetAttrsA = 127,
    SYS_GT_DrawBevelBoxA = 128,
    SYS_GT_GetGadgetAttrsA = 129,
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
#define SYS_Gfx_TextLength 74
#define SYS_Gfx_Text 75
#define SYS_Gfx_SetFont 76
#define SYS_Gfx_OpenFont 77
#define SYS_Gfx_CloseFont 78
#define SYS_Gfx_InitArea 79
#define SYS_Gfx_AreaMove 80
#define SYS_Gfx_AreaDraw 81
#define SYS_Gfx_AreaEnd 82
#define SYS_OpenWindowTagList 83
#define SYS_ModifyIDCMP 84
#define SYS_MoveWindow 85
#define SYS_SizeWindow 86
#define SYS_WindowToFront 87
#define SYS_WindowToBack 88
#define SYS_SetWindowTitles 89
#define SYS_ActivateWindow 90
#define SYS_SetMenuStrip 91
#define SYS_ClearMenuStrip 92
#define SYS_ItemAddress 93
#define SYS_CurrentTime 94
#define SYS_DoubleClick 95
#define SYS_DisplayBeep 96
#define SYS_ReportMouse 97
#define SYS_IntuiTextLength 98
#define SYS_PrintIText 99
#define SYS_DrawBorder 100
#define SYS_AddGList 101
#define SYS_RemoveGList 102
#define SYS_OnGadget 103
#define SYS_OffGadget 104
#define SYS_RefreshGList 105
#define SYS_RefreshWindowFrame 106
#define SYS_AutoRequest 107
#define SYS_EasyRequestArgs 108
#define SYS_OpenScreen 109
#define SYS_CloseScreen 110
#define SYS_OpenScreenTagList 111
#define SYS_OpenDevice 112
#define SYS_CloseDevice 113
#define SYS_DoIO 114
#define SYS_SendIO 115
#define SYS_CheckIO 116
#define SYS_WaitIO 117
#define SYS_AbortIO 118
#define SYS_FindClass 119
#define SYS_AddClass 120
#define SYS_RemoveClass 121
#define SYS_GT_GetVisualInfoA 122
#define SYS_GT_FreeVisualInfo 123
#define SYS_GT_CreateContext 124
#define SYS_GT_FreeGadgets 125
#define SYS_GT_CreateGadgetA 126
#define SYS_GT_SetGadgetAttrsA 127
#define SYS_GT_DrawBevelBoxA 128
#define SYS_GT_GetGadgetAttrsA 129
#endif

#endif
