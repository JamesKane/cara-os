// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(loadseg_runcommand): the T.3.2 milestone-in-miniature — the
// AmigaDOS launch path end to end, with no .incbin shortcut on the run
// side. We seed the dhrystone ELF onto CaraFS as the file "dhrystone"
// (raw Carafs_* writes, like KERNEL_TEST(carafs_io)), then spawn the
// `runseg` launcher Gleas. runseg LoadSeg()s "dhrystone" *off CaraFS* and
// RunCommand()s it with a command tail; RunCommand spawns the loaded image
// as a child Process, blocks on its exit, and returns its status. runseg
// exits with that status, so asserting runseg exited 0 proves the whole
// chain: file → LoadSeg → spawn child + argv → run to completion → join.
//
// Needs the NVMe device (CaraFS mount), same caveat as the carafs_* tests.

#include <cara/carafs.h>
#include <cara/carafs_bind.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __runseg_elf_start[];
extern char __runseg_elf_end[];
extern char __dhrystone_elf_start[];
extern char __dhrystone_elf_end[];

KERNEL_TEST(loadseg_runcommand)
{
    TEST_ASSERT(ctx, g_carafs_mounted, "CaraFS not mounted (no nvme device?)");
    u64 root = g_carafs.sb.root_cnode;

    // Seed "dhrystone" onto CaraFS from the embedded blob. Idempotent
    // across reboots: drop any leftover first.
    usize dhry_size = (usize)(__dhrystone_elf_end - __dhrystone_elf_start);
    TEST_ASSERT(ctx, dhry_size >= 64, "embedded dhrystone.elf too small");

    u64 cn;
    if (Carafs_DirLookup(&g_carafs, root, "dhrystone", 9, &cn, nullptr) == CARA_EOK) {
        TEST_ASSERT(ctx, Carafs_DirRemove(&g_carafs, root, "dhrystone", 9) == CARA_EOK,
                    "clear stale dhrystone");
    }
    TEST_ASSERT(ctx,
                Carafs_DirCreate(&g_carafs, root, "dhrystone", 9, CARAFS_T_FILE, &cn) == CARA_EOK,
                "create dhrystone file");
    TEST_ASSERT(ctx,
                Carafs_FileWrite(&g_carafs, cn, 0, __dhrystone_elf_start, dhry_size) == CARA_EOK,
                "write dhrystone file");

    // Spawn the launcher. It is a top-level task (no exit waiter), so its
    // SYS_EXIT trips the global UserExited flag; the dhrystone child it
    // RunCommand()s has a waiter and uses the kernel join path instead, so
    // it does not disturb this flag.
    Croi_Syscall_ResetUserExit();
    usize runseg_size = (usize)(__runseg_elf_end - __runseg_elf_start);
    TEST_ASSERT(ctx, runseg_size >= 64, "embedded runseg.elf too small");

    struct Task *launcher = Croi_SpawnUserTaskFromElf("runseg", 5, __runseg_elf_start, runseg_size);
    TEST_ASSERT(ctx, launcher != nullptr, "Croi_SpawnUserTaskFromElf(runseg) failed");

    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    i64 status = Croi_Syscall_UserExitStatus();
    LOG_INFO("lseg", "runseg (LoadSeg+RunCommand dhrystone from CaraFS) exited %lld", (i64)status);
    TEST_ASSERT(ctx, status == 0, "launcher did not exit 0 (load/run/join failed)");

    // Tidy up so a reboot starts clean.
    (void)Carafs_DirRemove(&g_carafs, root, "dhrystone", 9);
}
