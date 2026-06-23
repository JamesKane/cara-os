// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for icon.library (Phase 3 L11, LVO.md
// §5.2). Each sets a7 = SYS_Icon_* and ecalls into Croi; the generated
// icon_vec.c points each `syscall` vec slot here. Section
// `.lib_text.icon` is folded by croi.lds into the shared 0x4000_0000
// library RX page; icon.library's base + vec live in the SASOS shared
// heap.

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

.section .lib_text.icon, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_GetDiskObject,  SYS_Icon_GetDiskObject
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_FreeDiskObject, SYS_Icon_FreeDiskObject
