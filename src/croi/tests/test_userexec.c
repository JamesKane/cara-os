// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(userexec_smoke): the LVO.md §9 worked example.
// Spawns userexec.elf as a U-mode task; that program is linked
// against libcara, which runs _start before main:
//
//   1. _start inline-ecalls SYS_OpenLibrary("exec.library", 0)
//      and assigns the result to the SysBase global.
//   2. main() reads SysBase->LibNode.lib_Version (a plain memory
//      load — proof the user PT install at 0x4000_0000 worked).
//   3. main() calls OpenLibrary / AllocMem / FreeMem / CloseLibrary
//      via the generated <proto/exec.h> inline stubs (proof the
//      vec table indexing + trampoline ecalls + dispatcher arms
//      all work end-to-end).
//   4. main() returns USEREXEC_EXIT_OK; libcara tail-calls SYS_EXIT.
//
// This is the milestone the Phase A/B/C/D plan was written to hit.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __userexec_elf_start[];
extern char __userexec_elf_end[];

#define USEREXEC_EXIT_OK 0xCA1A

KERNEL_TEST(userexec_smoke)
{
    Croi_Syscall_ResetUserExit();

    usize elf_size = (usize)(__userexec_elf_end - __userexec_elf_start);
    TEST_ASSERT(ctx, elf_size >= 64, "embedded userexec.elf blob too small");

    TEST_ASSERT(ctx, (u8)__userexec_elf_start[0] == 0x7F
                         && __userexec_elf_start[1] == 'E'
                         && __userexec_elf_start[2] == 'L'
                         && __userexec_elf_start[3] == 'F',
                "userexec blob does not start with \\x7fELF");

    struct Task *user = Croi_SpawnUserTaskFromElf(
        "uexec", 5,
        __userexec_elf_start, elf_size);
    TEST_ASSERT(ctx, user != nullptr,
                "Croi_SpawnUserTaskFromElf(userexec) failed");

    // Drop our priority so the user task can actually run.
    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    i64 status = Croi_Syscall_UserExitStatus();
    if (status != USEREXEC_EXIT_OK) {
        // Surface the actual exit code so the failure is debuggable
        // — userexec returns distinct USEREXEC_EXIT_BAD_* values for
        // each assertion. Status is the i32 sign-extended return of
        // main(), so cast to short for display.
        LOG_ERROR("uxsm", "userexec.elf exited with 0x%llx (expected 0x%x)",
                  (u64)status, USEREXEC_EXIT_OK);
        TEST_FAIL(ctx, "userexec did not exit with USEREXEC_EXIT_OK");
    }
}
