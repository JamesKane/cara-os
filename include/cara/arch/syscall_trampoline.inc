/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * cara/arch/syscall_trampoline.h — the per-LVO syscall-trampoline macro as a
 * CARA_ARCH-selected dispatch header (epic H.7.6). The per-library
 * trampolines.S files #include this and instantiate CARA_SYSCALL_TRAMPOLINE for
 * each `syscall`-flavour LVO; the macro emits the right instruction sequence for
 * the build's ISA (RISC-V `li a7,N; ecall; ret` vs AArch64 `mov x8,#N; svc #0;
 * ret`). One ISA per build image, like the rest of the HAL (cara/arch.h).
 *
 * Selection mirrors <cara/arch_pte.h>: the kernel build defines CARA_ARCH_ARM64
 * for the AArch64 backend; everything else falls through to RISC-V.
 */

#ifndef CARA_ARCH_SYSCALL_TRAMPOLINE_H
#define CARA_ARCH_SYSCALL_TRAMPOLINE_H

#if defined(CARA_ARCH_ARM64)
#include <cara/arch/arm64/syscall_trampoline.inc>
#else
#include <cara/arch/riscv64/syscall_trampoline.inc>
#endif

#endif /* CARA_ARCH_SYSCALL_TRAMPOLINE_H */
