// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(usermode_smoke): spawn the embedded U-mode program,
// wait for its SYS_EXIT to fire, and assert the exit status. The
// user program (src/croi/user_program.S) issues SYS_LOG_WRITE
// "hello from U-mode" and then SYS_EXIT 42 — so a successful run
// proves U-mode entry, ecall trap routing, syscall dispatch, copy
// from user-VA via SUM, and the SYS_EXIT switch back to kernel.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __user_text_start[];
extern char __user_text_end[];

KERNEL_TEST(usermode_smoke)
{
    Croi_Syscall_ResetUserExit();

    usize ut_size = (usize)(__user_text_end - __user_text_start);
    TEST_ASSERT(ctx, ut_size > 0, "user program section empty");

    struct Task *user = Croi_SpawnUserTask(
        "uhi", 5,
        __user_text_start, ut_size,
        /*user_entry_va=*/ 0x10000ull);
    TEST_ASSERT(ctx, user != nullptr, "Croi_SpawnUserTask failed");

    // Drop kmain below the user task's priority so the user runs.
    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    TEST_ASSERT(ctx, Croi_Syscall_UserExitStatus() == 42,
                "user task did not exit with status 42");
}
