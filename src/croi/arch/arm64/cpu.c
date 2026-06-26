// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 CPU control — the arch CPU-control seam (epic H.7.1, docs/ARM64.md).
// The RISC-V analogue is arch/riscv64/cpu.c (wfi + sstatus.SIE); here the
// interrupt mask is DAIF (the I bit = IRQ).

#include <cara/arch.h>

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
    // Unmask IRQ (DAIF.I). FIQ stays masked — CaraOS routes everything
    // through IRQ, like the RISC-V kernel uses a single S-mode interrupt path.
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

void arch_irq_disable(void)
{
    __asm__ volatile("msr daifset, #2" ::: "memory");
}
