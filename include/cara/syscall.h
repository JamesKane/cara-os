// SPDX-License-Identifier: BSD-2-Clause
//
// Cara syscall ABI — the U-mode → S-mode entry surface.
//
// Calling convention:
//   a7 = syscall number (SYS_*)
//   a0..a5 = args
//   a0 = return value (negative = -CARA_E* error)
//   ecall traps; sepc points at the ecall, the dispatcher advances by 4.

#ifndef CARA_SYSCALL_H
#define CARA_SYSCALL_H

#include <cara/types.h>

#define SYS_LOG_WRITE 1
#define SYS_EXIT      2

// Internal API: invoked by the trap dispatcher when scause = 8 (ecall
// from U-mode). Reads syscall number/args from the trap frame, runs
// the matching handler, returns the result (placed back into frame->x[10]
// by the caller).
struct TrapFrame;
i64 Croi_Syscall_Dispatch(struct TrapFrame *frame);

// Test/observability hooks — set by SYS_EXIT, polled by usermode_smoke.
bool Croi_Syscall_UserExited(void);
i64  Croi_Syscall_UserExitStatus(void);
void Croi_Syscall_ResetUserExit(void);

#endif
