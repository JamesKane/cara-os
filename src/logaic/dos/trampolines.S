// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for dos.library's `syscall`-flavour rows
// (L3.1: just IoErr; the packet ops are `server` flavour and don't
// trampoline here). Like the exec/intuition/utility trampolines these
// live in the shared RX page at 0x4000_0000 (section .lib_text.dos,
// placed by croi.lds). The generated dos_vec.c points the IoErr vec slot
// at this; calling IoErr() via <proto/dos.h> reaches here, the trampoline
// ecalls into Croi, and src/croi/syscall/syscall.c routes by a7.

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

.section .lib_text.dos, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosIoErr, SYS_Dos_IoErr
