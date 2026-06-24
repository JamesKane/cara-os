// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(amicalc_gui): the T.4.4 milestone — a real third-party AmigaOS
// GUI application (amiCalc, MIT, vendored verbatim under ports/amicalc/) built
// UNEDITED against the CaraOS SDK, opening its window and responding to input.
//
// Headless-friendly: we stand up a SYNTHETIC in-RAM framebuffer (no real
// display) + a Workbench screen, spawn amiCalc, and let it run. amiCalc opens
// its window, draws its display + button grid via graphics.library, then
// blocks in WaitPort — so once control returns to us its UI is rendered. We
// assert (a) it opened a window and (b) the framebuffer changed (it actually
// drew), then post IDCMP_CLOSEWINDOW and assert it processes that input and
// exits 0. Proves the whole chain: build-unedited → open → render → respond.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>

extern char __amicalc_elf_start[];
extern char __amicalc_elf_end[];

// FNV-1a over the framebuffer bytes — to detect that amiCalc rendered.
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

KERNEL_TEST(amicalc_gui)
{
    Croi_Syscall_ResetUserExit();
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);

    usize elf_size = (usize)(__amicalc_elf_end - __amicalc_elf_start);
    TEST_ASSERT(ctx, elf_size >= 64, "embedded amicalc.elf too small");
    TEST_ASSERT(ctx,
                (u8)__amicalc_elf_start[0] == 0x7F && __amicalc_elf_start[1] == 'E' &&
                    __amicalc_elf_start[2] == 'L' && __amicalc_elf_start[3] == 'F',
                "amicalc blob not an ELF");

    // amiCalc opens a 300x200 window at (50,50); give the screen room for it.
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx, Dath_AllocBitmap(&fb, 380, 270, DATH_FMT_RGBA8888) == CARA_EOK,
                "framebuffer alloc");
    struct Screen *scr = Leargas_OpenScreen(&fb, "Workbench", Dath_RGB(0x10, 0x20, 0x40));
    TEST_ASSERT(ctx, scr != nullptr, "open screen");

    u64 hash_bg = fb_hash(&fb); // screen background, before amiCalc draws

    struct Task *app = Croi_SpawnUserTaskFromElf("amicalc", 5, __amicalc_elf_start, elf_size);
    TEST_ASSERT(ctx, app != nullptr, "spawn amicalc");

    // amiCalc runs (opens window, draws, then blocks in WaitPort) above our
    // priority, so dropping ours hands it the CPU; it returns control once it
    // is blocked waiting for input — by which point its UI is fully drawn.
    Croi_TaskSetSelfPriority(-1);
    int spins = 0;
    while (!Croi_Syscall_UserExited() && Leargas_ActiveWindow() == nullptr) {
        Croi_Yield();
        if (++spins > 100000) {
            break;
        }
    }
    struct Window *win = Leargas_ActiveWindow();
    TEST_ASSERT(ctx, !Croi_Syscall_UserExited() && win != nullptr, "amiCalc opened its window");

    // It rendered its UI (window chrome + display + buttons) into our fb.
    u64 hash_drawn = fb_hash(&fb);
    TEST_ASSERT(ctx, hash_drawn != hash_bg, "amiCalc rendered to the screen");

    // Respond to input: deliver IDCMP_CLOSEWINDOW (as the close gadget would).
    // amiCalc's event loop sees it, tears down, and exits 0.
    TEST_ASSERT(ctx, Leargas_IDCMP_PostCloseWindow(win), "post CLOSEWINDOW");
    spins = 0;
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
        if (++spins > 100000) {
            break;
        }
    }
    Croi_TaskSetSelfPriority(100);

    TEST_ASSERT(ctx, Croi_Syscall_UserExited(), "amiCalc exited after CLOSEWINDOW");
    i64 status = Croi_Syscall_UserExitStatus();
    LOG_INFO("amic", "amiCalc (unedited MIT port) opened, rendered, closed; exit %lld",
             (i64)status);
    TEST_ASSERT(ctx, status == 0, "amiCalc did not exit 0");

    Leargas_CloseScreen(scr);
    Dath_FreeBitmap(&fb);
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);
}
