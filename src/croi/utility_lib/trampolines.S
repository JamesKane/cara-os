// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for utility.library's allocating tag
// helpers (the rest of utility.library is `local` flavour — see
// tag_ops.c — and never traps). Like the exec/intuition trampolines
// these live in the shared RX page at 0x4000_0000 (section
// .lib_text.utility, placed by croi.lds). The generated utility_vec.c
// points the AllocateTagItems/CloneTagItems/FreeTagItems/
// RefreshTagItemClones vec slots at these; calling the LVO via the
// <proto/utility.h> inline stub reaches here, the trampoline ecalls
// into Croi, and src/croi/syscall/syscall.c routes by a7.

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

.section .lib_text.utility, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_AllocateTagItems,     SYS_AllocateTagItems
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_CloneTagItems,        SYS_CloneTagItems
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_FreeTagItems,         SYS_FreeTagItems
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_RefreshTagItemClones, SYS_RefreshTagItemClones
