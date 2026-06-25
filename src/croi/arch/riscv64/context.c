// SPDX-License-Identifier: BSD-2-Clause
//
// RISC-V task context setup — the first-dispatch + U-mode-entry seam (epic H,
// H.3). arch_ctx_init_kernel/user prime a freshly spawned task's saved
// register area so the first arch_ctx_switch lands in the right place;
// user_task_trampoline is that landing point for U-mode tasks (it sets up
// sstatus/sepc/sscratch + the page table and sret's into the user entry).
// The integer/FP switch itself + task_trampoline (the kernel-task landing
// point) are in ctx_switch.S. The portable scheduler (sched.c) drives all of
// this without touching the RISC-V register layout.

#include <cara/arch.h>
#include <cara/sched.h>
#include <cara/types.h>

// saved-register layout (mirrors ctx_switch.S): [0]=ra, [1]=sp, [2]=gp,
// [3]=tp, [4..15]=s0..s11, [16]=sscratch.
#define SR_RA 0
#define SR_SP 1
#define SR_SSCRATCH 16

extern void task_trampoline(void); // kernel-task landing point (ctx_switch.S)
[[noreturn]] void user_task_trampoline(void);

// Prime a kernel task: first switch returns into task_trampoline on the
// kstack; sscratch = 0 is the S-mode invariant (trap_entry.S's origin marker).
void arch_ctx_init_kernel(u64 *saved, u64 kstack_top)
{
    saved[SR_RA] = (u64)(uptr)task_trampoline;
    saved[SR_SP] = kstack_top;
    saved[SR_SSCRATCH] = 0;
}

// Prime a U-mode task: first switch returns into user_task_trampoline; both
// sp and sscratch are the kstack top (the U-mode sscratch convention so a
// user-origin trap swaps onto the kernel stack — trap_entry.S).
void arch_ctx_init_user(u64 *saved, u64 kstack_top)
{
    saved[SR_RA] = (u64)(uptr)user_task_trampoline;
    saved[SR_SP] = kstack_top;
    saved[SR_SSCRATCH] = kstack_top;
}

[[noreturn]] void user_task_trampoline(void)
{
    struct Task *t = Sched_Current();

    // Switch to the user's page table (arch MMU-activate primitive, H.2).
    arch_mmu_activate(t->user_pt);

    // sscratch = kstack_top (U-mode invariant). arch_ctx_switch already set
    // this from saved[16] when it dispatched us here, but writing it again is
    // defensive against any later refactor of that path.
    u64 kstack_top = (u64)(uptr)t->kstack + t->kstack_size;
    kstack_top &= ~15ull;
    __asm__ volatile("csrw sscratch, %0" : : "r"(kstack_top) : "memory");

    // sstatus: SPP=0 (sret returns to U), SPIE=1 (user gets SIE), SUM=1
    // (S-mode can read U-mode pages), FS=Initial (bits[14:13]=01) so the task
    // may execute FP instructions (T.4.1 — U-mode FPU). Without FS != Off a
    // U-mode fadd.d/fmul.d traps illegal. The FP register file is preserved
    // across switches by arch_ctx_switch.
    u64 sstatus_val = (1ull << 5) | (1ull << 18) | (1ull << 13);
    __asm__ volatile("csrw sstatus, %0" : : "r"(sstatus_val) : "memory");

    // sepc = user entry.
    __asm__ volatile("csrw sepc, %0" : : "r"(t->user_entry) : "memory");

    // Initial register file: sp = user stack top; a0/a1 = the entry args
    // (T.3.2 — command-line pointer + length, which libcara turns into argv;
    // zero for tasks spawned without a command line). Pin them to fixed
    // registers so the asm can consume them before clearing the scratch
    // registers. Everything else starts zero.
    register u64 _sp __asm__("s1") = t->user_sp_top;
    register u64 _a0 __asm__("s2") = t->user_a0;
    register u64 _a1 __asm__("s3") = t->user_a1;
    __asm__ volatile("mv  sp, %0\n"
                     "mv  a0, %1\n"
                     "mv  a1, %2\n"
                     "li  ra, 0\n"
                     "li  gp, 0\n"
                     "li  tp, 0\n"
                     "li  t0, 0\n"
                     "li  t1, 0\n"
                     "li  t2, 0\n"
                     "li  t3, 0\n"
                     "li  t4, 0\n"
                     "li  t5, 0\n"
                     "li  t6, 0\n"
                     "li  a2, 0\n"
                     "li  a3, 0\n"
                     "li  a4, 0\n"
                     "li  a5, 0\n"
                     "li  a6, 0\n"
                     "li  a7, 0\n"
                     "li  s0, 0\n"
                     "li  s1, 0\n"
                     "li  s2, 0\n"
                     "li  s3, 0\n"
                     "li  s4, 0\n"
                     "li  s5, 0\n"
                     "li  s6, 0\n"
                     "li  s7, 0\n"
                     "li  s8, 0\n"
                     "li  s9, 0\n"
                     "li  s10, 0\n"
                     "li  s11, 0\n"
                     "sret\n"
                     :
                     : "r"(_sp), "r"(_a0), "r"(_a1)
                     : "memory");

    __builtin_unreachable();
}
