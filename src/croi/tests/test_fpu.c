// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(fpu_umode): T.4.1 — U-mode floating point works and survives
// context switches. Spawns the `fputest` Gleas, which accumulates in
// `double` across many Delay()-driven yields and exits 0 iff the result is
// exact. Meanwhile this runner hammers the FPU from the *kernel* side
// (kacc) between yields, so if arch_ctx_switch failed to save/restore the
// FP register file, the two would corrupt each other and fputest would exit
// non-zero. Proving both: (a) U-mode FP is enabled (sstatus.FS), and (b) the
// FP file round-trips across switches. The substrate the amiCalc port needs.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __fputest_elf_start[];
extern char __fputest_elf_end[];

KERNEL_TEST(fpu_umode)
{
    Croi_Syscall_ResetUserExit();

    usize sz = (usize)(__fputest_elf_end - __fputest_elf_start);
    TEST_ASSERT(ctx, sz >= 64, "embedded fputest.elf too small");

    struct Task *t = Croi_SpawnUserTaskFromElf("fputest", 5, __fputest_elf_start, sz);
    TEST_ASSERT(ctx, t != nullptr, "Croi_SpawnUserTaskFromElf(fputest) failed");

    // Kernel-side FP pressure: touch the FPU between every switch so a
    // missing FP save/restore in arch_ctx_switch would clobber fputest's
    // registers (and vice versa). Match fputest's priority (5) so the two
    // round-robin and genuinely interleave their FP contexts through
    // ctx_switch on every switch. `volatile` keeps it real, not folded.
    volatile double kacc = 1.0;
    Croi_TaskSetSelfPriority(5);
    while (!Croi_Syscall_UserExited()) {
        kacc = kacc * 1.000001 + 0.5;
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    TEST_ASSERT(ctx, kacc > 1.0, "kernel-side FP did not run (no interleave)");

    i64 status = Croi_Syscall_UserExitStatus();
    LOG_INFO("fpu ", "fputest exited %lld (kernel kacc=%ld)", (i64)status, (long)kacc);
    TEST_ASSERT(ctx, status == 0, "U-mode FP result wrong across context switches");
}
