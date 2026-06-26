// SPDX-License-Identifier: BSD-2-Clause
//
// TEMPORARY (epic H.7.3+). The portable Croi_TrapDispatch (src/croi/trap.c) is
// linked into the AArch64 kernel, but the time layer (cara_time) is not yet
// ported to arm64 — it arrives with the scheduler integration. This stand-in
// satisfies Croi_TrapDispatch's Croi_Time_OnTimerTrap extern. (The demo
// Croi_Syscall_Dispatch lives in boot.c, where the enter-U-mode demo state is.)
// DELETE this file when cara_time is brought into the arm64 link.

#include <cara/types.h>

// No timer source is enabled until H.7.5; arch_trap_is_timer() never returns
// true, so this is never called — present only to resolve the extern.
void Croi_Time_OnTimerTrap(void);
void Croi_Time_OnTimerTrap(void)
{
}
