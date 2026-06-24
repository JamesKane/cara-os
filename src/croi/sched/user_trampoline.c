// SPDX-License-Identifier: BSD-2-Clause
//
// Initial dispatch trampoline for U-mode tasks. Runs in S-mode the
// first time the task is selected by the scheduler — switches to the
// task's user page table, sets up sstatus / sepc / sscratch / sp, and
// sret's into the user entry. From there on, each U→S→U round-trip
// goes through trap_entry.S; this trampoline only fires once per task.

#include <cara/mm.h>
#include <cara/sched.h>
#include <cara/types.h>

[[noreturn]] void user_task_trampoline(void);

[[noreturn]] void user_task_trampoline(void)
{
    struct Task *t = Sched_Current();

    // Switch to the user's page table.
    u64 satp = Sv39_Satp(t->user_pt);
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
    __asm__ volatile("csrw satp, %0" : : "r"(satp) : "memory");
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");

    // sscratch = kstack_top (U-mode invariant). ctx_switch already set
    // this from saved_regs[16] when it dispatched us here, but writing
    // it again is defensive against any later refactor of that path.
    u64 kstack_top = (u64)(uptr)t->kstack + t->kstack_size;
    kstack_top &= ~15ull;
    __asm__ volatile("csrw sscratch, %0" : : "r"(kstack_top) : "memory");

    // sstatus: SPP=0 (sret returns to U), SPIE=1 (user gets SIE), SUM=1
    // (S-mode can read U-mode pages — kept in case a syscall handler
    // needs it before re-entering kernel mode), FS=Initial (bits[14:13]=01)
    // so the task may execute FP instructions (T.4.1 — U-mode FPU). Without
    // FS != Off a U-mode fadd.d/fmul.d traps illegal. The FP register file
    // is preserved across switches by croi_ctx_switch.
    u64 sstatus_val = (1ull << 5) | (1ull << 18) | (1ull << 13);
    __asm__ volatile("csrw sstatus, %0" : : "r"(sstatus_val) : "memory");

    // sepc = user entry.
    __asm__ volatile("csrw sepc, %0" : : "r"(t->user_entry) : "memory");

    // Initial register file: sp = user stack top; a0/a1 = the entry args
    // (T.3.2 — command-line pointer + length, which libcara turns into
    // argv; zero for tasks spawned without a command line). Pin them to
    // fixed registers so the asm can consume them before clearing the
    // scratch registers. Everything else starts zero.
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
