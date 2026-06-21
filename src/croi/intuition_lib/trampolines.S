// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for intuition.library (Phase 3, LVO.md §5.2).
//
// These mirror src/croi/exec_lib/trampolines.S exactly: each is a tiny
// U-mode-executable stub that sets a7 = SYS_<Name> and ecalls into Croi.
// The generated intuition_vec.c points each `syscall`-flavour vec slot
// at the matching trampoline here.
//
// Placement: the stubs live in section `.lib_text.intuition`, which
// croi.lds folds into the SAME 0x4000_0000 library RX page region as
// exec's `.lib_text.exec` (one shared RX+U region mapped into every
// task PT). intuition.library's *base + vec table* live in the SASOS
// shared heap (0x1_0000_0000), but the vec entries point here — the
// vec table just holds function pointers into this RX region.
//
// Calling convention: the <proto/intuition.h> inline stub passes user
// args verbatim in a0..a5 (followed by IntuitionBase as the trailing
// arg, ignored by syscall flavour). The trampoline preserves them,
// sets a7, and ecalls; the dispatcher (src/croi/syscall/syscall.c)
// routes by a7 and the return value comes back in a0.

#include <cara/sysno.h>

.macro CARA_SYSCALL_TRAMPOLINE name, num
    .global \name
    .type   \name, @function
    \name:
        li      a7, \num
        ecall
        ret
    .size   \name, . - \name
.endm

.section .lib_text.intuition, "ax"
.balign 4

// ---- Windows ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_OpenWindow,        SYS_OpenWindow
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_CloseWindow,       SYS_CloseWindow
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_OpenWindowTagList, SYS_OpenWindowTagList
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ModifyIDCMP,       SYS_ModifyIDCMP
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_MoveWindow,        SYS_MoveWindow
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_SizeWindow,        SYS_SizeWindow
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_WindowToFront,     SYS_WindowToFront
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_WindowToBack,      SYS_WindowToBack
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_SetWindowTitles,   SYS_SetWindowTitles
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ActivateWindow,    SYS_ActivateWindow
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_SetMenuStrip,      SYS_SetMenuStrip
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ClearMenuStrip,    SYS_ClearMenuStrip
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ItemAddress,       SYS_ItemAddress
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_CurrentTime,       SYS_CurrentTime
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DoubleClick,       SYS_DoubleClick
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DisplayBeep,       SYS_DisplayBeep
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ReportMouse,       SYS_ReportMouse
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_IntuiTextLength,   SYS_IntuiTextLength
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_PrintIText,        SYS_PrintIText
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DrawBorder,        SYS_DrawBorder
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_AddGList,          SYS_AddGList
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_RemoveGList,       SYS_RemoveGList
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_OnGadget,          SYS_OnGadget
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_OffGadget,         SYS_OffGadget
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_RefreshGList,      SYS_RefreshGList
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_RefreshWindowFrame, SYS_RefreshWindowFrame

// ---- Gadgets ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_AddGadget,       SYS_AddGadget
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_RemoveGadget,    SYS_RemoveGadget
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ActivateGadget,  SYS_ActivateGadget
