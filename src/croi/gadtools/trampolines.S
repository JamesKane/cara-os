// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for gadtools.library (Phase 3 L8, LVO.md
// §5.2). Each is a tiny U-mode-executable stub that sets a7 = SYS_GT_*
// and ecalls into Croi; the generated gadtools_vec.c points each
// `syscall`-flavour vec slot at the matching trampoline here.
//
// Placement: section `.lib_text.gadtools`, folded by croi.lds into the
// shared 0x4000_0000 library RX page (like exec/intuition/graphics).
// gadtools.library's base + vec live in the SASOS shared heap; the vec
// entries point here.

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

.section .lib_text.gadtools, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_FreeGadgets,     SYS_GT_FreeGadgets
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_CreateContext,   SYS_GT_CreateContext
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_GetVisualInfoA,  SYS_GT_GetVisualInfoA
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_FreeVisualInfo,  SYS_GT_FreeVisualInfo
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_CreateGadgetA,    SYS_GT_CreateGadgetA
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_SetGadgetAttrsA,  SYS_GT_SetGadgetAttrsA
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_DrawBevelBoxA,    SYS_GT_DrawBevelBoxA
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_GetGadgetAttrsA,  SYS_GT_GetGadgetAttrsA
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_CreateMenusA,     SYS_GT_CreateMenusA
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_FreeMenus,        SYS_GT_FreeMenus
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_LayoutMenuItemsA, SYS_GT_LayoutMenuItemsA
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_LayoutMenusA,     SYS_GT_LayoutMenusA
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_GetIMsg,          SYS_GT_GetIMsg
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_ReplyIMsg,        SYS_GT_ReplyIMsg
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_RefreshWindow,    SYS_GT_RefreshWindow
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_BeginRefresh,     SYS_GT_BeginRefresh
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GT_EndRefresh,       SYS_GT_EndRefresh
