// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 generic timer + GICv2 interrupt path (epic H.7.5, docs/ARM64.md §3).
// The RISC-V analogue is arch/riscv64/timer.c (the `time` CSR + Sstc); here the
// counter is CNTVCT_EL0 and the one-shot deadline is CNTV_CVAL_EL0 + the virtual
// timer control CNTV_CTL_EL0, delivered as PPI 27 through the GIC.
//
// Unlike RISC-V (one cause register, no interrupt controller to ack), AArch64
// IRQs arrive on a separate vector and must be acknowledged/EOI'd at the GIC.
// So the IRQ path is handled here (arm64_irq_dispatch, called from the IRQ
// vector in trap_entry.S) rather than through the portable Croi_TrapDispatch —
// which stays for synchronous traps (syscalls). The timer IRQ ultimately drives
// the scheduler tick (Croi_Time_OnTimerTrap) once the scheduler is ported.

#include <cara/arch.h>
#include <cara/time.h> // Croi_Time_OnTimerTrap (portable time layer, H.7.7a)
#include <cara/trap.h>
#include <cara/types.h>

// GICv2 on QEMU virt (fixed early addresses, like the PL011 console; the
// FDT-discovered values supersede these once the real driver lands). Reached
// through the kernel upper-half direct map (TTBR1) so they survive TTBR0 swaps.
#define CARA_KERNEL_VA_OFFSET 0xFFFFFFC000000000ull
static volatile u32 *const gicd = (volatile u32 *)(uptr)(0x08000000ull + CARA_KERNEL_VA_OFFSET);
static volatile u32 *const gicc = (volatile u32 *)(uptr)(0x08010000ull + CARA_KERNEL_VA_OFFSET);
#define GICD_CTLR (0x000 / 4)
#define GICD_ISENABLER (0x100 / 4) // + (irq/32)
#define GICC_CTLR (0x000 / 4)
#define GICC_PMR (0x004 / 4)
#define GICC_IAR (0x00C / 4)
#define GICC_EOIR (0x010 / 4)

#define ARM64_VIRT_TIMER_PPI 27u // CNTV interrupt (PPI 11 = INTID 27)
#define GIC_SPURIOUS 1023u

#define CNTV_CTL_ENABLE 1u // bit0 ENABLE, bit1 IMASK, bit2 ISTATUS (RO)
#define CNTV_CTL_IMASK 2u

// ---- arch_timer_* seam (cara/arch.h) --------------------------------------

u64 arch_timer_ticks(void)
{
    u64 v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

void arch_timer_arm(u64 deadline_ticks)
{
    __asm__ volatile("msr cntv_cval_el0, %0" : : "r"(deadline_ticks) : "memory");
    u64 ctl = CNTV_CTL_ENABLE; // enable, IMASK=0
    __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"(ctl) : "memory");
}

void arch_timer_disarm(void)
{
    u64 ctl = CNTV_CTL_IMASK; // masked + disabled
    __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"(ctl) : "memory");
}

// ---- GIC + IRQ dispatch ---------------------------------------------------

// Timebase frequency (Hz) of the generic timer.
u64 arm64_timer_freq(void);
u64 arm64_timer_freq(void)
{
    u64 f;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
    return f;
}

// Bring up the GICv2 distributor + CPU interface and enable the virtual timer
// PPI. Single-security view on QEMU virt (secure=off): enable group 0.
void arm64_gic_init(void);
void arm64_gic_init(void)
{
    gicd[GICD_CTLR] = 1;   // enable distributor
    gicc[GICC_PMR] = 0xF0; // priority mask: allow all
    gicc[GICC_CTLR] = 1;   // enable CPU interface
    gicd[GICD_ISENABLER + (ARM64_VIRT_TIMER_PPI / 32)] = 1u << (ARM64_VIRT_TIMER_PPI % 32);
}

// IRQ entry — called from the IRQ vector (trap_entry.S) with x0 = TrapFrame*.
// Acks the GIC, routes the virtual-timer PPI to the portable time layer
// (Croi_Time_OnTimerTrap disarms the source + flags the deadline), and EOIs.
void arm64_irq_dispatch(struct TrapFrame *frame);
void arm64_irq_dispatch(struct TrapFrame *frame)
{
    (void)frame;
    u32 iar = gicc[GICC_IAR];
    u32 id = iar & 0x3FF;
    if (id >= 1020) {
        return; // spurious (1023) / special — no EOI
    }
    if (id == ARM64_VIRT_TIMER_PPI) {
        Croi_Time_OnTimerTrap();
    }
    gicc[GICC_EOIR] = iar;
}
