// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(libctest_smoke): spawns libctest.elf — a U-mode Gleas that
// exercises the Phase T userland libc (string/stdlib/stdio/ctype) the way a
// ported app will — and asserts it exits LIBCTEST_OK. A non-OK status is
// the number of the failing check inside libctest.c. Mirrors
// test_userexec.c.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __libctest_elf_start[];
extern char __libctest_elf_end[];

#define LIBCTEST_OK 0x1BC0

KERNEL_TEST(libctest_smoke)
{
    Croi_Syscall_ResetUserExit();

    usize elf_size = (usize)(__libctest_elf_end - __libctest_elf_start);
    TEST_ASSERT(ctx, elf_size >= 64, "embedded libctest.elf blob too small");

    struct Task *user = Croi_SpawnUserTaskFromElf("libctest", 5, __libctest_elf_start, elf_size);
    TEST_ASSERT(ctx, user != nullptr, "Croi_SpawnUserTaskFromElf(libctest) failed");

    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    i64 status = Croi_Syscall_UserExitStatus();
    if (status != LIBCTEST_OK) {
        LOG_ERROR("libc",
                  "libctest.elf exited with 0x%llx (expected 0x%x); status is the failing "
                  "check number",
                  (u64)status, LIBCTEST_OK);
        TEST_FAIL(ctx, "libctest did not pass all libc checks");
    }
}
