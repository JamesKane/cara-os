// SPDX-License-Identifier: BSD-2-Clause
//
// RISC-V (S-mode) CPU control — the arch_* CPU seam (epic H, H.1). halt/idle
// via WFI; global interrupt enable/disable via sstatus.SIE.

#include <cara/arch.h>
#include <cara/types.h>

#define SSTATUS_SIE (1ull << 1)

CARA_NORETURN void arch_halt(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}

void arch_idle(void)
{
    __asm__ volatile("wfi");
}

void arch_irq_enable(void)
{
    __asm__ volatile("csrs sstatus, %0" : : "r"((u64)SSTATUS_SIE) : "memory");
}

void arch_irq_disable(void)
{
    __asm__ volatile("csrc sstatus, %0" : : "r"((u64)SSTATUS_SIE) : "memory");
}
