// SPDX-License-Identifier: BSD-2-Clause
//
// S-mode time and Sstc deadline timer.
//
// `Croi_Time_Now()` reads the standard `time` CSR (0xC01) and converts
// to nanoseconds using the timebase from /cpus/timebase-frequency.
// `Croi_Time_SetDeadline(ns)` writes Sstc's `stimecmp` (0x14D) and
// enables sie.STIE so the deadline raises a supervisor timer trap.
//
// Sstc is required; Croi_Time_Init asserts its presence in the
// platform record. SBI's TIMER extension is not used: Sstc is in the
// X1's reported ISA and QEMU's OpenSBI surfaces it.

#ifndef CARA_TIME_H
#define CARA_TIME_H

#include <cara/types.h>

void Croi_Time_Init(u64 timebase_hz);

// Current S-mode time in nanoseconds. Monotonic since boot.
u64 Croi_Time_Now(void);

// Schedule a one-shot supervisor timer trap to fire when Croi_Time_Now()
// >= deadline_ns. Enables sie.STIE; the trap handler clears it. Calling
// this again before the trap arrives just rewrites stimecmp.
void Croi_Time_SetDeadline(u64 deadline_ns);

// Clear sie.STIE; cancel pending deadline. Idempotent.
void Croi_Time_CancelDeadline(void);

// One-shot ack from the timer trap path. Cleared after read; reading
// from outside the trap context is an immediate poll.
bool Croi_Time_DeadlineFired(void);

// Called from Croi_TrapDispatch on a supervisor timer interrupt.
// Disables sie.STIE so the same deadline doesn't keep firing, and
// records that a deadline arrived for the foreground waiter.
void Croi_Time_OnTimerTrap(void);

#endif
