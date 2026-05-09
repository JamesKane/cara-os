// SPDX-License-Identifier: BSD-2-Clause

#include <cara/time.h>
#include <cara/types.h>

// CSR numbers (RISC-V Privileged + Sstc).
#define CSR_TIME 0xC01
#define CSR_STIMECMP 0x14D
#define CSR_SIE 0x104
#define CSR_SSTATUS 0x100

// Bit positions in sie / sip / sstatus.
#define SIE_STIE (1ull << 5)
#define SSTATUS_SIE (1ull << 1)

static u64 g_timebase_hz = 0;
static volatile bool g_deadline_fired = false;

static inline u64 csr_time(void)
{
    u64 v;
    __asm__ volatile("csrr %0, 0xC01" : "=r"(v));
    return v;
}

static inline void csr_set_sie_stie(void)
{
    u64 mask = SIE_STIE;
    __asm__ volatile("csrs sie, %0" : : "r"(mask) : "memory");
}

static inline void csr_clr_sie_stie(void)
{
    u64 mask = SIE_STIE;
    __asm__ volatile("csrc sie, %0" : : "r"(mask) : "memory");
}

static inline void csr_set_sstatus_sie(void)
{
    u64 mask = SSTATUS_SIE;
    __asm__ volatile("csrs sstatus, %0" : : "r"(mask) : "memory");
}

static inline void csr_write_stimecmp(u64 v)
{
    __asm__ volatile("csrw 0x14D, %0" : : "r"(v) : "memory");
}

void Croi_Time_Init(u64 timebase_hz)
{
    g_timebase_hz = timebase_hz;
    g_deadline_fired = false;
    csr_clr_sie_stie();
    // Globally enable supervisor interrupts. Individual sources still
    // need their sie bits set before they can fire.
    csr_set_sstatus_sie();
}

u64 Croi_Time_Now(void)
{
    if (g_timebase_hz == 0) {
        return 0;
    }
    u64 t = csr_time();
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
    csr_write_stimecmp(ns_to_ticks(deadline_ns));
    csr_set_sie_stie();
}

void Croi_Time_CancelDeadline(void)
{
    csr_clr_sie_stie();
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
    // firing until the next stimecmp write. Foreground code clears the
    // flag and may schedule another deadline.
    csr_clr_sie_stie();
    g_deadline_fired = true;
}
