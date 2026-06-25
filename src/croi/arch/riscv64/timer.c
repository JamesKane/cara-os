// SPDX-License-Identifier: BSD-2-Clause
//
// RISC-V free-running timer — the arch_timer_* seam (epic H, H.1). The
// counter is the `time` CSR (0xC01); the one-shot deadline is Sstc's
// `stimecmp` (0x14D) plus sie.STIE (the supervisor timer interrupt enable).
// The portable Croi_Time layer (src/croi/time.c) does the ns<->ticks math
// over these. Sstc is required (X1 reports it; QEMU's OpenSBI surfaces it).

#include <cara/arch.h>
#include <cara/types.h>

#define SIE_STIE (1ull << 5)

u64 arch_timer_ticks(void)
{
    u64 v;
    __asm__ volatile("csrr %0, 0xC01" : "=r"(v)); // CSR_TIME
    return v;
}

void arch_timer_arm(u64 deadline_ticks)
{
    __asm__ volatile("csrw 0x14D, %0" : : "r"(deadline_ticks) : "memory"); // stimecmp
    __asm__ volatile("csrs sie, %0" : : "r"((u64)SIE_STIE) : "memory");
}

void arch_timer_disarm(void)
{
    __asm__ volatile("csrc sie, %0" : : "r"((u64)SIE_STIE) : "memory");
}
