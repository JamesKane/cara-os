// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for expansion.library (Phase 3 L14, LVO.md
// §5.2). Each sets a7 = SYS_Exp_* and ecalls into Croi; the generated
// expansion_vec.c points each `syscall` vec slot here. Section
// `.lib_text.expansion` is folded by croi.lds into the shared 0x4000_0000
// library RX page; expansion.library's base + vec live in the SASOS shared
// heap.

#include <cara/sysno.h>

#include <cara/arch/syscall_trampoline.inc>

.section .lib_text.expansion, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Exp_AddConfigDev,   SYS_Exp_AddConfigDev
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Exp_AllocConfigDev, SYS_Exp_AllocConfigDev
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Exp_FindConfigDev,  SYS_Exp_FindConfigDev
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Exp_FreeConfigDev,  SYS_Exp_FreeConfigDev
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Exp_RemConfigDev,   SYS_Exp_RemConfigDev
