// SPDX-License-Identifier: BSD-2-Clause
//
// TEMPORARY (epic H.7.3). The portable Croi_TrapDispatch (src/croi/trap.c) is
// now linked into the AArch64 kernel, but the real syscall dispatch table
// (cara_syscall) and the time layer (cara_time) are not yet ported to arm64
// — they arrive with the scheduler in H.7.4. These two stand-ins satisfy
// Croi_TrapDispatch's externs so the trap path can be exercised end to end
// (boot.c issues an svc and checks the round-trip). DELETE this file in H.7.4
// when cara_syscall + cara_time are brought into the arm64 link.

#include <cara/arch.h>
#include <cara/trap.h>
#include <cara/types.h>

// Demo syscall: echo back (number + arg0) so the svc round-trip is observable.
// The real table-driven dispatcher (Croi_Syscall_Dispatch in cara_syscall)
// replaces this in H.7.4.
i64 Croi_Syscall_Dispatch(struct TrapFrame *frame);
i64 Croi_Syscall_Dispatch(struct TrapFrame *frame)
{
    return (i64)(arch_syscall_num(frame) + arch_syscall_arg(frame, 0));
}

// No timer source is enabled until H.7.5; arch_trap_is_timer() never returns
// true, so this is never called — present only to resolve the extern.
void Croi_Time_OnTimerTrap(void);
void Croi_Time_OnTimerTrap(void)
{
}
