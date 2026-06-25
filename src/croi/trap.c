// SPDX-License-Identifier: BSD-2-Clause
//
// Portable S-mode trap dispatch (epic H, H.4). The arch entry (arch/<arch>/
// trap_entry.S) builds a TrapFrame and calls here; we classify via the arch
// accessors (cara/arch.h) and route — syscall to the dispatch table, timer to
// the time layer, anything else to the arch fatal dump. No raw cause/CSR
// knowledge lives here.

#include <cara/arch.h>
#include <cara/syscall.h>
#include <cara/time.h>
#include <cara/trap.h>
#include <cara/types.h>

void Croi_TrapDispatch(struct TrapFrame *frame)
{
    if (arch_trap_is_syscall(frame)) {
        i64 ret = Croi_Syscall_Dispatch(frame);
        arch_syscall_set_ret(frame, ret);
        arch_trap_advance(frame); // step past the syscall instruction
        return;
    }
    if (arch_trap_is_timer(frame)) {
        Croi_Time_OnTimerTrap();
        return;
    }
    arch_trap_fatal(frame); // [[noreturn]]: dump + halt
}
