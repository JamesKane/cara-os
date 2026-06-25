// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(shell_launches_gui): the hardened live launch path (T.5),
// proven headlessly. This is what a real ramfb boot does — a Workbench screen
// is up and the console Shell launches a GUI app onto it — but driven by
// injected input instead of a display + keyboard:
//   1. seed C/amicalc and stand up a synthetic in-RAM Workbench screen,
//   2. feed the console "amicalc\nquit\n" and spawn the Shell,
//   3. the Shell reads "amicalc", LoadSeg+RunCommands it; amiCalc opens its
//      window on the Workbench and renders,
//   4. we post IDCMP_CLOSEWINDOW; amiCalc exits, RunCommand returns, the Shell
//      reads "quit" and exits 0.
// Asserting the Shell exits 0, that two programs ran (Shell + amiCalc), and
// that the screen changed proves the whole chain console→shell→LoadSeg→
// RunCommand→GUI-on-Workbench→input→exit.

#include <cara/carafs_bind.h>
#include <cara/console_input.h>
#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __shell_elf_start[];
extern char __shell_elf_end[];
extern char __amicalc_elf_start[];
extern char __amicalc_elf_end[];

static u64 fb_hash(const struct DathFramebuffer *fb)
{
    const u8 *p = (const u8 *)fb->base;
    usize n = (usize)fb->stride * fb->height;
    u64 h = 1469598103934665603ull;
    for (usize i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

KERNEL_TEST(shell_launches_gui)
{
    TEST_ASSERT(ctx, g_carafs_mounted, "CaraFS not mounted (no nvme device?)");

    usize ami_sz = (usize)(__amicalc_elf_end - __amicalc_elf_start);
    TEST_ASSERT(ctx, ami_sz >= 64, "embedded amicalc.elf too small");
    Croi_Boot_SeedCommand("amicalc", 7, __amicalc_elf_start, ami_sz);

    // Synthetic Workbench (no real display). Routers are armed at boot; we
    // post CLOSEWINDOW directly below, so their state does not matter here.
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);

    struct DathFramebuffer fb;
    TEST_ASSERT(ctx, Dath_AllocBitmap(&fb, 380, 270, DATH_FMT_RGBA8888) == CARA_EOK,
                "framebuffer alloc");
    struct Screen *scr = Leargas_OpenScreen(&fb, "Workbench", Dath_RGB(0x10, 0x20, 0x40));
    TEST_ASSERT(ctx, scr != nullptr, "open screen");
    u64 hash_bg = fb_hash(&fb);

    // Drive the shell: launch amicalc, then quit.
    Croi_ConsoleInput_Inject("amicalc\nquit\n", 13);

    i64 before = Croi_Syscall_UserExitCount();
    Croi_Syscall_ResetUserExit();

    usize sh_sz = (usize)(__shell_elf_end - __shell_elf_start);
    struct Task *sh = Croi_SpawnUserTaskFromElf("shtgui", 5, __shell_elf_start, sh_sz);
    TEST_ASSERT(ctx, sh != nullptr, "spawn shell");

    // Let the shell read "amicalc" and RunCommand it; amiCalc opens its window
    // on the Workbench (the active window) and renders, then blocks in WaitPort.
    Croi_TaskSetSelfPriority(-1);
    int spins = 0;
    while (!Croi_Syscall_UserExited() && Leargas_ActiveWindow() == nullptr) {
        Croi_Yield();
        if (++spins > 100000) {
            break;
        }
    }
    struct Window *win = Leargas_ActiveWindow();
    TEST_ASSERT(ctx, !Croi_Syscall_UserExited() && win != nullptr,
                "amiCalc opened its window from the shell");
    TEST_ASSERT(ctx, fb_hash(&fb) != hash_bg, "amiCalc rendered on the Workbench");

    // Close it: amiCalc exits, RunCommand returns, the shell reads "quit" + exits.
    TEST_ASSERT(ctx, Leargas_IDCMP_PostCloseWindow(win), "post CLOSEWINDOW");
    spins = 0;
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
        if (++spins > 100000) {
            break;
        }
    }
    Croi_TaskSetSelfPriority(100);

    TEST_ASSERT(ctx, Croi_Syscall_UserExited(), "shell exited");
    i64 status = Croi_Syscall_UserExitStatus();
    i64 ran = Croi_Syscall_UserExitCount() - before;
    LOG_INFO("shgu",
             "shell launched amiCalc on the Workbench; shell exit %lld, %lld program(s) ran",
             (i64)status, ran);
    TEST_ASSERT(ctx, status == 0, "shell did not exit 0");
    TEST_ASSERT(ctx, ran >= 2, "amiCalc did not run via the shell (expected shell + amicalc)");

    Leargas_CloseScreen(scr);
    Dath_FreeBitmap(&fb);
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);
}
