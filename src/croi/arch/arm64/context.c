// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 task context setup — the first-dispatch + U-mode-entry seam (epic
// H.7.7d). arch_ctx_init_kernel/user prime a freshly spawned task's saved
// register area so the first arch_ctx_switch lands in the right place;
// user_task_trampoline is that landing point for U-mode tasks (it sets up the
// EL0 exception state + the task's page table and `eret`s into the user entry).
// The integer/FP switch itself + task_trampoline (the kernel-task landing
// point) are in ctx_switch.S. The portable scheduler (sched.c) drives all of
// this via the arch_ctx_* seam without touching the AArch64 register layout.
//
// saved_regs layout (mirrors ctx_switch.S): [0..11] = x19..x30, [12] = sp,
// [13] = tpidr_el1.

#include <cara/arch.h>
#include <cara/mm.h> // struct Task.user_pt (struct PageTable)
#include <cara/sched.h>
#include <cara/types.h>

#define SR_X30 11 // LR — where `ret` in arch_ctx_switch lands
#define SR_SP 12
#define SR_TPIDR 13

extern void task_trampoline(void); // kernel-task landing point (ctx_switch.S)
[[noreturn]] void user_task_trampoline(void);

// Prime a kernel task: the first switch `ret`s into task_trampoline on the new
// kstack (→ Sched_Trampoline → entry_fn).
void arch_ctx_init_kernel(u64 *saved, u64 kstack_top)
{
    saved[SR_X30] = (u64)(uptr)task_trampoline;
    saved[SR_SP] = kstack_top;
    saved[SR_TPIDR] = 0;
}

// Prime a U-mode task: the first switch `ret`s into user_task_trampoline, which
// drops to EL0. sp = the task's kstack top (the kernel stack a later EL0-origin
// trap will use; the AArch64 CPU auto-selects SP_EL1, so no sscratch dance).
void arch_ctx_init_user(u64 *saved, u64 kstack_top)
{
    saved[SR_X30] = (u64)(uptr)user_task_trampoline;
    saved[SR_SP] = kstack_top;
    saved[SR_TPIDR] = 0;
}

[[noreturn]] void user_task_trampoline(void)
{
    struct Task *t = Sched_Current();

    // Switch to the task's address space (its TTBR0; the kernel half is TTBR1).
    arch_mmu_activate(t->user_pt);

    u64 ktop = ((u64)(uptr)t->kstack + t->kstack_size) & ~15ull;

    // Pin the values we need into callee-saved regs so the final asm can set the
    // EL0 state, then clear the rest of the GPR file before `eret` (no kernel
    // values leak into U-mode). SPSR = EL0t (M=0) with DAIF clear (the task runs
    // with interrupts enabled so the timer can preempt it).
    register u64 r_ktop __asm__("x19") = ktop;
    register u64 r_usp __asm__("x20") = t->user_sp_top;
    register u64 r_elr __asm__("x21") = t->user_entry;
    register u64 r_a0 __asm__("x22") = t->user_a0;
    register u64 r_a1 __asm__("x23") = t->user_a1;
    register u64 r_spsr __asm__("x24") = 0;

    __asm__ volatile("mov sp, %[ktop]\n\t" // SP_EL1 = clean kstack top
                     "msr sp_el0, %[usp]\n\t"
                     "msr elr_el1, %[elr]\n\t"
                     "msr spsr_el1, %[spsr]\n\t"
                     "mov x0, %[a0]\n\t"
                     "mov x1, %[a1]\n\t"
                     // Clear x2..x30 (the pinned inputs x19..x24 are consumed).
                     "mov x2, xzr\n\t mov x3, xzr\n\t mov x4, xzr\n\t mov x5, xzr\n\t"
                     "mov x6, xzr\n\t mov x7, xzr\n\t mov x8, xzr\n\t mov x9, xzr\n\t"
                     "mov x10, xzr\n\t mov x11, xzr\n\t mov x12, xzr\n\t mov x13, xzr\n\t"
                     "mov x14, xzr\n\t mov x15, xzr\n\t mov x16, xzr\n\t mov x17, xzr\n\t"
                     "mov x18, xzr\n\t mov x19, xzr\n\t mov x20, xzr\n\t mov x21, xzr\n\t"
                     "mov x22, xzr\n\t mov x23, xzr\n\t mov x24, xzr\n\t mov x25, xzr\n\t"
                     "mov x26, xzr\n\t mov x27, xzr\n\t mov x28, xzr\n\t mov x29, xzr\n\t"
                     "mov x30, xzr\n\t"
                     "isb\n\t"
                     "eret\n"
                     :
                     : [ktop] "r"(r_ktop), [usp] "r"(r_usp), [elr] "r"(r_elr), [spsr] "r"(r_spsr),
                       [a0] "r"(r_a0), [a1] "r"(r_a1)
                     : "memory", "x0", "x1");
    __builtin_unreachable();
}
