// SPDX-License-Identifier: BSD-2-Clause
//
// Portable time + one-shot deadline timer. The ns<->ticks math and the
// deadline bookkeeping live here; the raw counter + compare + interrupt
// enable are the arch's (arch_timer_* / arch_irq_enable, epic H, H.1).

#include <cara/arch.h>
#include <cara/time.h>
#include <cara/types.h>

static u64 g_timebase_hz = 0;
static volatile bool g_deadline_fired = false;

void Croi_Time_Init(u64 timebase_hz)
{
    g_timebase_hz = timebase_hz;
    g_deadline_fired = false;
    arch_timer_disarm();
    // Globally enable interrupts. Individual sources still need their own
    // enable (the timer source is armed by Croi_Time_SetDeadline).
    arch_irq_enable();
}

u64 Croi_Time_Now(void)
{
    if (g_timebase_hz == 0) {
        return 0;
    }
    u64 t = arch_timer_ticks();
    // ns = t * 1e9 / timebase_hz. Decompose to keep intermediate values
    // under 2^64. At 24 MHz this is exact in u64 for ~12 minutes; longer
    // uptimes need 128-bit math (deferred).
    u64 sec = t / g_timebase_hz;
    u64 rem = t - sec * g_timebase_hz;
    return sec * 1000000000ull + (rem * 1000000000ull) / g_timebase_hz;
}

static u64 ns_to_ticks(u64 ns)
{
    if (g_timebase_hz == 0) {
        return 0;
    }
    u64 sec = ns / 1000000000ull;
    u64 rem = ns - sec * 1000000000ull;
    return sec * g_timebase_hz + (rem * g_timebase_hz) / 1000000000ull;
}

void Croi_Time_SetDeadline(u64 deadline_ns)
{
    g_deadline_fired = false;
    arch_timer_arm(ns_to_ticks(deadline_ns));
}

void Croi_Time_CancelDeadline(void)
{
    arch_timer_disarm();
}

bool Croi_Time_DeadlineFired(void)
{
    bool fired = g_deadline_fired;
    g_deadline_fired = false;
    return fired;
}

void Croi_Time_OnTimerTrap(void)
{
    // Disable the source so the (still-pending) interrupt doesn't keep
    // firing until the next deadline is armed. Foreground code clears the
    // flag and may schedule another deadline.
    arch_timer_disarm();
    g_deadline_fired = true;
}
