/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * RISC-V per-LVO syscall-trampoline macro — one of the CARA_ARCH-selected
 * variants behind <cara/arch/syscall_trampoline.h> (epic H.7.6). Included by
 * the per-library trampolines.S; each instantiation emits a tiny
 * U-mode-executable stub in the library's RX page that sets a7 = syscall
 * number and ecalls into Croi (LVO.md §5.2). Args pass through in a0..a5; the
 * return comes back in a0; `ret` returns to the inline proto-header stub.
 */

#ifndef CARA_ARCH_RISCV64_SYSCALL_TRAMPOLINE_H
#define CARA_ARCH_RISCV64_SYSCALL_TRAMPOLINE_H

.macro CARA_SYSCALL_TRAMPOLINE name, num
    .global \name
    .type   \name, @function
    \name:
        li      a7, \num
        ecall
        ret
    .size   \name, . - \name
.endm

#endif /* CARA_ARCH_RISCV64_SYSCALL_TRAMPOLINE_H */
