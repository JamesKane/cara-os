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

// Arch-shaped saved register frame. This is the RISC-V layout — its field
// offsets match arch/riscv64/trap_entry.S exactly (32*8 + 4*8 = 288 bytes).
// Portable code treats it opaquely via the arch_trap_*/arch_syscall_*
// accessors; a second arch (H.7) defines its own shape.
struct TrapFrame {
    u64 x[32]; // x[0] is reserved (always 0); x[2] is sp at trap entry
    u64 sepc;
    u64 scause;
    u64 stval;
    u64 sstatus;
};

// Trap dispatch entry called from the arch trap entry. Routes syscall / timer
// / fatal via the arch accessors; handled paths return so the asm tail can
// return from trap, the fatal path halts.
void Croi_TrapDispatch(struct TrapFrame *frame);

#endif
