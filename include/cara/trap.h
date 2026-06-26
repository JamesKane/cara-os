// SPDX-License-Identifier: BSD-2-Clause
//
// S-mode trap handling. The arch trap entry (arch/<arch>/trap_entry.S) saves
// a TrapFrame on the current stack, calls the portable Croi_TrapDispatch,
// restores, and returns from trap. Dispatch classifies via the arch
// accessors (cara/arch.h: arch_trap_is_syscall/is_timer/…) and routes.
//
// The vector install (arch_trap_init) lives in cara/arch.h.

#ifndef CARA_TRAP_H
#define CARA_TRAP_H

#include <cara/types.h>

// Arch-shaped saved register frame — selected by CARA_ARCH (the kernel build
// defines CARA_ARCH_ARM64 for the AArch64 backend; everything else is RISC-V).
// Each layout's field offsets match its arch/<arch>/trap_entry.S exactly.
// Portable code (Croi_TrapDispatch) treats it opaquely via the
// arch_trap_*/arch_syscall_* accessors (cara/arch.h).
#if defined(CARA_ARCH_ARM64)
// AArch64: x0–x30 + the EL1 exception state. `sp` is the pre-trap stack (SP_EL0
// for an EL0 origin, else the EL1 sp); `kind` is a synthetic vector-group tag
// (0=sync 1=irq 2=fiq 3=serror) set by the vector stub, since AArch64 tells
// sync-vs-IRQ by *vector*, not a syndrome bit. 288 bytes (16-byte aligned).
struct TrapFrame {
    u64 x[31]; // x0..x30
    u64 sp;    // SP at trap entry
    u64 elr;   // ELR_EL1 (return address; already past svc for a syscall)
    u64 spsr;  // SPSR_EL1 (saved PSTATE)
    u64 esr;   // ESR_EL1 (syndrome; EC in [31:26])
    u64 kind;  // 0=sync 1=irq 2=fiq 3=serror
};
#else
// RISC-V Sv39 layout — offsets match arch/riscv64/trap_entry.S (32*8 + 4*8 =
// 288 bytes).
struct TrapFrame {
    u64 x[32]; // x[0] is reserved (always 0); x[2] is sp at trap entry
    u64 sepc;
    u64 scause;
    u64 stval;
    u64 sstatus;
};
#endif

// Trap dispatch entry called from the arch trap entry. Routes syscall / timer
// / fatal via the arch accessors; handled paths return so the asm tail can
// return from trap, the fatal path halts.
void Croi_TrapDispatch(struct TrapFrame *frame);

#endif
