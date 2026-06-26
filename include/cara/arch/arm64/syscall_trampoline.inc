/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * AArch64 per-LVO syscall-trampoline macro — one of the CARA_ARCH-selected
 * variants behind <cara/arch/syscall_trampoline.h> (epic H.7.6). The AArch64
 * analogue of the RISC-V trampoline: set x8 = syscall number and `svc #0`
 * instead of a7 + ecall. Args pass through in x0..x5; the return comes back in
 * x0; `ret` returns to the inline proto-header stub.
 */

#ifndef CARA_ARCH_ARM64_SYSCALL_TRAMPOLINE_H
#define CARA_ARCH_ARM64_SYSCALL_TRAMPOLINE_H

.macro CARA_SYSCALL_TRAMPOLINE name, num
    .global \name
    .type   \name, @function
    \name:
        mov     x8, #\num
        svc     #0
        ret
    .size   \name, . - \name
.endm

#endif /* CARA_ARCH_ARM64_SYSCALL_TRAMPOLINE_H */
