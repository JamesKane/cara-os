// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for commodities.library (Phase 3 L13,
// LVO.md §5.2). Each sets a7 = SYS_Cx_* and ecalls into Croi; the generated
// commodities_vec.c points each `syscall` vec slot here. Section
// `.lib_text.commodities` is folded by croi.lds into the shared
// 0x4000_0000 library RX page; commodities.library's base + vec live in the
// SASOS shared heap.

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

.section .lib_text.commodities, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_CreateCxObj,     SYS_Cx_CreateCxObj
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_CxBroker,        SYS_Cx_CxBroker
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_ActivateCxObj,   SYS_Cx_ActivateCxObj
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_DeleteCxObj,     SYS_Cx_DeleteCxObj
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_DeleteCxObjAll,  SYS_Cx_DeleteCxObjAll
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_CxObjType,       SYS_Cx_CxObjType
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_CxObjError,      SYS_Cx_CxObjError
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_ClearCxObjError, SYS_Cx_ClearCxObjError
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_SetCxObjPri,     SYS_Cx_SetCxObjPri
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_AttachCxObj,     SYS_Cx_AttachCxObj
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_EnqueueCxObj,    SYS_Cx_EnqueueCxObj
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_InsertCxObj,     SYS_Cx_InsertCxObj
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_RemoveCxObj,     SYS_Cx_RemoveCxObj
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_SetTranslate,    SYS_Cx_SetTranslate
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_SetFilter,       SYS_Cx_SetFilter
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_SetFilterIX,     SYS_Cx_SetFilterIX
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_ParseIX,         SYS_Cx_ParseIX
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_DisposeCxMsg,    SYS_Cx_DisposeCxMsg
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_CxMsgType,       SYS_Cx_CxMsgType
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_CxMsgData,       SYS_Cx_CxMsgData
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Cx_CxMsgID,         SYS_Cx_CxMsgID
