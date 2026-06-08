// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(userintuition_smoke): the I3 worked example. Proves the
// full U-mode → intuition.library → Leargas chain end-to-end.
//
// The boot path opens no screen under -nographic, so the test first
// stands up an active screen over a static framebuffer (the same way
// the host leargas tests do). It then spawns userintuition.elf, a real
// U-mode Gleas linked against libcara, which:
//
//   1. OpenLibrary("intuition.library") via the exec.library syscall
//      path and assigns IntuitionBase (proof the library was
//      constructed in the shared heap and registered).
//   2. OpenWindow(&nw) with Screen=nullptr — the <proto/intuition.h>
//      stub indexes the shared-heap vec table, JALRs to the trampoline
//      in the 0x4000_0000 RX page, which ecalls SYS_OpenWindow; the
//      dispatcher routes to Croi_OpenWindow_Impl → Leargas_OpenWindow.
//      A non-null return proves the bridge reaches the substrate and
//      the SASOS Window pointer round-trips back to U-mode.
//   3. CloseWindow / CloseLibrary, returns USERINT_EXIT_OK; libcara
//      tail-calls SYS_EXIT and the test asserts the status.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __userintuition_elf_start[];
extern char __userintuition_elf_end[];

#define USERINT_EXIT_OK 0xC1A7

// Static backing surface for the test screen. Generous enough to hold
// the window userintuition opens at (20,20) size 120x60.
static u32 g_userintu_fb_storage[160 * 120];

KERNEL_TEST(userintuition_smoke)
{
    Croi_Syscall_ResetUserExit();

    usize elf_size = (usize)(__userintuition_elf_end - __userintuition_elf_start);
    TEST_ASSERT(ctx, elf_size >= 64, "embedded userintuition.elf blob too small");
    TEST_ASSERT(ctx,
                (u8)__userintuition_elf_start[0] == 0x7F && __userintuition_elf_start[1] == 'E' &&
                    __userintuition_elf_start[2] == 'L' && __userintuition_elf_start[3] == 'F',
                "userintuition blob does not start with \\x7fELF");

    // Stand up an active screen over the static framebuffer. InitInPlace
    // sets the public Screen defaults without painting or allocating;
    // SetActive makes Leargas_ActiveScreen() resolve it, which is what
    // OpenWindow(Screen=nullptr) targets.
    struct DathFramebuffer fb;
    int rc = Dath_Framebuffer_Init(&fb, g_userintu_fb_storage, 160, 120, 160 * 4,
                                   DATH_FMT_RGBA8888);
    TEST_ASSERT(ctx, rc == CARA_EOK, "Dath_Framebuffer_Init failed");

    static struct LeargasScreen screen;
    rc = Leargas_Screen_InitInPlace(&screen, &fb, "Workbench", 0xFF101020u);
    TEST_ASSERT(ctx, rc == CARA_EOK, "Leargas_Screen_InitInPlace failed");
    Leargas_Screen_SetActive(&screen);

    struct Task *user = Croi_SpawnUserTaskFromElf("uintu", 5, __userintuition_elf_start, elf_size);
    TEST_ASSERT(ctx, user != nullptr, "Croi_SpawnUserTaskFromElf(userintuition) failed");

    Croi_TaskSetSelfPriority(-1);
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    i64 status = Croi_Syscall_UserExitStatus();

    // Restore the no-active-screen baseline so later tests / the boot
    // path are unaffected.
    Leargas_Screen_SetActive(nullptr);

    if (status != USERINT_EXIT_OK) {
        LOG_ERROR("uism", "userintuition.elf exited with 0x%llx (expected 0x%x)", (u64)status,
                  USERINT_EXIT_OK);
        TEST_FAIL(ctx, "userintuition did not exit with USERINT_EXIT_OK");
    }
}
