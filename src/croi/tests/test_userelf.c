// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(userelf_smoke): spawn the embedded userhello.elf via the
// ELF loader path. The user binary is linked at user VA 0x10000, does
// SYS_LOG_WRITE "hello from a real ELF in U-mode", then SYS_EXIT 1234.
// Asserts the loader correctly mapped PT_LOAD segments, the user
// program reached its second syscall, and the exit status round-trips.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __userhello_elf_start[];
extern char __userhello_elf_end[];

KERNEL_TEST(userelf_smoke)
{
    Croi_Syscall_ResetUserExit();

    usize elf_size = (usize)(__userhello_elf_end - __userhello_elf_start);
    TEST_ASSERT(ctx, elf_size >= 64, "embedded ELF blob too small");

    // The first four bytes must be the ELF magic.
    TEST_ASSERT(ctx, (u8)__userhello_elf_start[0] == 0x7F
                         && __userhello_elf_start[1] == 'E'
                         && __userhello_elf_start[2] == 'L'
                         && __userhello_elf_start[3] == 'F',
                "blob does not start with \\x7fELF");

    struct Task *user = Croi_SpawnUserTaskFromElf(
        "uelf", 5,
        __userhello_elf_start, elf_size);
    TEST_ASSERT(ctx, user != nullptr, "Croi_SpawnUserTaskFromElf failed");

    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    TEST_ASSERT(ctx, Croi_Syscall_UserExitStatus() == 1234,
                "userhello.elf did not exit with status 1234");
}
