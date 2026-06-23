// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(dhrystone_smoke): the Phase T milestone-in-miniature — a
// real, third-party, unedited AmigaOS-era C program (Dhrystone C/1.1,
// 1984, vendored verbatim under ports/dhrystone/) built against the CaraOS
// SDK + libc and run as a U-mode Gleas. It prints its result lines to the
// console (visible in the QEMU log) and exit(0)s on completion; this test
// asserts it ran to completion without trapping and exited cleanly. The
// dhrystones/second figure is non-deterministic under TCG, so it is logged,
// not asserted.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __dhrystone_elf_start[];
extern char __dhrystone_elf_end[];

KERNEL_TEST(dhrystone_smoke)
{
    Croi_Syscall_ResetUserExit();

    usize elf_size = (usize)(__dhrystone_elf_end - __dhrystone_elf_start);
    TEST_ASSERT(ctx, elf_size >= 64, "embedded dhrystone.elf blob too small");
    TEST_ASSERT(ctx,
                (u8)__dhrystone_elf_start[0] == 0x7F && __dhrystone_elf_start[1] == 'E' &&
                    __dhrystone_elf_start[2] == 'L' && __dhrystone_elf_start[3] == 'F',
                "dhrystone blob not an ELF");

    struct Task *user = Croi_SpawnUserTaskFromElf("dhrystone", 5, __dhrystone_elf_start, elf_size);
    TEST_ASSERT(ctx, user != nullptr, "Croi_SpawnUserTaskFromElf(dhrystone) failed");

    // Run it to completion (a trap would never set UserExited → the smoke
    // harness times out, which is the failure signal).
    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    i64 status = Croi_Syscall_UserExitStatus();
    LOG_INFO("dhry", "dhrystone.elf exited with status %lld", (i64)status);
    TEST_ASSERT(ctx, status == 0, "dhrystone did not exit(0)");
}
