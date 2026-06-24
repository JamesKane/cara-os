// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(shell_runs_command): the T.3.3 milestone, driven headlessly.
// Seeds the dhrystone command into CaraFS C/, feeds the console two typed
// lines ("dhrystone" then "quit") via Croi_ConsoleInput_Inject, spawns
// CaraShell, and asserts the shell exited cleanly *and* that two programs
// ran (the shell itself plus the dhrystone it launched). The exit-count
// check is what proves dhrystone actually ran — a shell that failed to find
// the command would still exit 0, but only one program would have exited.
//
// This is the same chain the interactive boot does (boot → prompt → type
// "dhrystone" → it runs), minus a human at the keyboard. Needs the NVMe
// device (CaraFS mount), same caveat as the carafs_* tests.

#include <cara/carafs_bind.h>
#include <cara/console_input.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __shell_elf_start[];
extern char __shell_elf_end[];
extern char __dhrystone_elf_start[];
extern char __dhrystone_elf_end[];

KERNEL_TEST(shell_runs_command)
{
    TEST_ASSERT(ctx, g_carafs_mounted, "CaraFS not mounted (no nvme device?)");

    usize dsz = (usize)(__dhrystone_elf_end - __dhrystone_elf_start);
    TEST_ASSERT(ctx, dsz >= 64, "embedded dhrystone.elf too small");
    Croi_Boot_SeedCommand("dhrystone", 9, __dhrystone_elf_start, dsz);

    // Two typed lines: run dhrystone (resolved via the shell's C/ search,
    // since it is not at the root), then quit. dhrystone ignores argv; the
    // point is input → parse → LoadSeg(C/dhrystone) → RunCommand → join.
    Croi_ConsoleInput_Inject("dhrystone\nquit\n", 15);

    i64 before = Croi_Syscall_UserExitCount();
    Croi_Syscall_ResetUserExit();

    usize ssz = (usize)(__shell_elf_end - __shell_elf_start);
    TEST_ASSERT(ctx, ssz >= 64, "embedded shell.elf too small");
    struct Task *sh = Croi_SpawnUserTaskFromElf("shtest", 5, __shell_elf_start, ssz);
    TEST_ASSERT(ctx, sh != nullptr, "Croi_SpawnUserTaskFromElf(shell) failed");

    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    i64 status = Croi_Syscall_UserExitStatus();
    i64 ran = Croi_Syscall_UserExitCount() - before;
    LOG_INFO("shtl", "shell exited %lld; %lld program(s) ran (shell + the command it launched)",
             status, ran);
    TEST_ASSERT(ctx, status == 0, "CaraShell did not exit 0");
    TEST_ASSERT(ctx, ran >= 2, "dhrystone did not run via the shell (expected shell + child exit)");
}
