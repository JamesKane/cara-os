// SPDX-License-Identifier: BSD-2-Clause
//
// S-mode trap handling. The trap entry point (croi_trap_entry, in
// trap_entry.S) saves a TrapFrame on the current stack, calls
// Croi_TrapDispatch, restores, sret. For Tier 1 the dispatcher only
// classifies and panics — no async handling yet. Timer (Sstc) and
// external-interrupt routing (PLIC) plug in later in Epics A.4 and B.

#ifndef CARA_TRAP_H
#define CARA_TRAP_H

#include <cara/types.h>

// Layout must match the offsets used in src/croi/trap_entry.S exactly.
// Total size: 32*8 + 4*8 = 288 bytes.
struct TrapFrame {
    u64 x[32]; // x[0] is reserved (always 0); x[2] is sp at trap entry
    u64 sepc;
    u64 scause;
    u64 stval;
    u64 sstatus;
};

// Install croi_trap_entry into stvec (direct mode). Call once per hart
// during early init, before anything that could fault is executed.
void Croi_TrapInit(void);

// Trap dispatch entry called from trap_entry.S. Classifies scause and
// either handles or panics. [[noreturn]] paths halt; handled paths
// return so the asm tail can sret.
void Croi_TrapDispatch(struct TrapFrame *frame);

#endif
