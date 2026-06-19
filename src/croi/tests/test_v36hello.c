// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(v36hello_smoke): the Phase 3 P0 proof (docs/PHASE3.md §4
// P0). v36hello.elf is built from *verbatim V36 source* — it includes
// only <exec/*>, <intuition/*>, <proto/*>, no cara/* — so its existence
// as a linked ELF already proves "a V36 program compiles + links
// against CaraOS headers unmodified". This test proves the other half:
// it runs as a Gleas. We stand up an active screen (the boot path opens
// none under -nographic), spawn the ELF, and assert it returns
// RETURN_OK (0) — i.e. OpenLibrary("intuition.library", 36) + OpenWindow
// + CloseWindow + CloseLibrary all succeeded.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __v36hello_elf_start[];
extern char __v36hello_elf_end[];

static u32 g_v36hello_fb[160 * 120];

KERNEL_TEST(v36hello_smoke)
{
    Croi_Syscall_ResetUserExit();

    usize elf_size = (usize)(__v36hello_elf_end - __v36hello_elf_start);
    TEST_ASSERT(ctx, elf_size >= 64, "embedded v36hello.elf blob too small");
    TEST_ASSERT(ctx,
                (u8)__v36hello_elf_start[0] == 0x7F && __v36hello_elf_start[1] == 'E' &&
                    __v36hello_elf_start[2] == 'L' && __v36hello_elf_start[3] == 'F',
                "v36hello blob does not start with \\x7fELF");

    struct DathFramebuffer fb;
    int rc = Dath_Framebuffer_Init(&fb, g_v36hello_fb, 160, 120, 160 * 4, DATH_FMT_RGBA8888);
    TEST_ASSERT(ctx, rc == CARA_EOK, "Dath_Framebuffer_Init failed");
    static struct LeargasScreen screen;
    rc = Leargas_Screen_InitInPlace(&screen, &fb, "Workbench", 0xFF101020u);
    TEST_ASSERT(ctx, rc == CARA_EOK, "Leargas_Screen_InitInPlace failed");
    Leargas_Screen_SetActive(&screen);

    struct Task *user = Croi_SpawnUserTaskFromElf("v36h", 5, __v36hello_elf_start, elf_size);
    TEST_ASSERT(ctx, user != nullptr, "spawn v36hello failed");

    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    i64 status = Croi_Syscall_UserExitStatus();
    Leargas_Screen_SetActive(nullptr);

    if (status != 0) {
        LOG_ERROR("v36h", "v36hello.elf exited with 0x%llx (expected RETURN_OK=0)", (u64)status);
        TEST_FAIL(ctx, "verbatim-V36 app did not return RETURN_OK");
    }
}
