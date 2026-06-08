// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(clar_smoke): drives the Clar Workbench Gleas end-to-end.
// The test plays the role of the input source (the live boot path uses a
// kernel input-pump task fed by HID): it stands up a screen, spawns Clar
// as a U-mode task, waits for Clar to open its desktop window + drawer
// gadget, then injects a click on the drawer through the real input ring
// + router. Clar receives IDCMP_GADGETUP and exits CLAR_EXIT_OK, which
// this test asserts.
//
// Concurrency: Clar runs at a higher priority than the test, so after we
// drop our priority Clar gets the CPU, sets up its window, and blocks in
// WaitPort. That hands control back to us to inject input; the route
// signals Clar's UserPort and it wakes to process the message.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/syscall.h>
#include <cara/test.h>
#include <cara/types.h>
#include <devices/inputevent.h>

extern char __clar_elf_start[];
extern char __clar_elf_end[];

#define CLAR_EXIT_OK 0xC1A8

// Drawer-centre in screen-absolute coordinates — must match the geometry
// hard-coded in src/userland/clar.c (window (10,10), drawer window-rel
// (20,30) size 60x20 -> centre window-rel (50,40) -> absolute (60,50)).
#define CLAR_DRAWER_ABS_X 60
#define CLAR_DRAWER_ABS_Y 50

KERNEL_TEST(clar_smoke)
{
    Croi_Syscall_ResetUserExit();
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);
    // Install the full router set (the boot path does this too; be
    // self-contained so the test order doesn't matter).
    Leargas_SetKeyRouter(Leargas_IDCMP_RouteKey);
    Leargas_SetGadgetRouter(Leargas_IDCMP_PostGadgetUp);
    Leargas_SetMouseButtonRouter(Leargas_IDCMP_PostMouseButtons);
    Leargas_SetCloseWindowRouter(Leargas_IDCMP_PostCloseWindow);

    usize elf_size = (usize)(__clar_elf_end - __clar_elf_start);
    TEST_ASSERT(ctx, elf_size >= 64, "embedded clar.elf blob too small");
    TEST_ASSERT(ctx,
                (u8)__clar_elf_start[0] == 0x7F && __clar_elf_start[1] == 'E' &&
                    __clar_elf_start[2] == 'L' && __clar_elf_start[3] == 'F',
                "clar blob does not start with \\x7fELF");

    struct DathFramebuffer fb;
    TEST_ASSERT(ctx, Dath_AllocBitmap(&fb, 240, 140, DATH_FMT_RGBA8888) == CARA_EOK,
                "framebuffer alloc");
    struct Screen *scr = Leargas_OpenScreen(&fb, "Workbench", Dath_RGB(0x10, 0x20, 0x40));
    TEST_ASSERT(ctx, scr != nullptr, "open screen");

    struct DathFramebuffer save;
    TEST_ASSERT(ctx,
                Dath_AllocBitmap(&save, leargas_pointer_arrow.width, leargas_pointer_arrow.height,
                                 fb.format) == CARA_EOK,
                "pointer save alloc");
    struct LeargasPointer p;
    TEST_ASSERT(ctx,
                Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow,
                                     Dath_RGB(0xFF, 0xFF, 0xFF), Dath_RGB(0, 0, 0),
                                     CLAR_DRAWER_ABS_X, CLAR_DRAWER_ABS_Y) == CARA_EOK,
                "pointer init");

    struct Task *clar = Croi_SpawnUserTaskFromElf("clar", 5, __clar_elf_start, elf_size);
    TEST_ASSERT(ctx, clar != nullptr, "spawn clar");

    // Let Clar run: it opens its window + drawer gadget, then blocks in
    // WaitPort, returning control to us.
    Croi_TaskSetSelfPriority(-1);

    // Wait until Clar's desktop window + drawer gadget are up (or Clar
    // died early, in which case the exit-status assert below catches it).
    int spins = 0;
    while (!Croi_Syscall_UserExited()) {
        struct Window *aw = Leargas_ActiveWindow();
        if (aw && aw->FirstGadget) {
            break;
        }
        Croi_Yield();
        if (++spins > 100000) {
            break; // safety: don't hang the runner
        }
    }

    if (!Croi_Syscall_UserExited()) {
        struct Window *aw = Leargas_ActiveWindow();
        TEST_ASSERT(ctx, aw != nullptr && aw->FirstGadget != nullptr,
                    "Clar opened its desktop window + drawer gadget");

        // Inject the drawer click through the real ring + router: a
        // left-button press then release over the drawer. The release
        // posts IDCMP_GADGETUP to Clar's UserPort, waking it.
        struct LeargasInputEvent down = { .ie_class = IECLASS_RAWMOUSE, .ie_code = IECODE_LBUTTON };
        struct LeargasInputEvent up = { .ie_class = IECLASS_RAWMOUSE,
                                        .ie_code = (u16)(IECODE_LBUTTON | IECODE_UP_PREFIX) };
        (void)Leargas_Input_Post(&down);
        (void)Leargas_Input_Drain(&p);
        (void)Leargas_Input_Post(&up);
        (void)Leargas_Input_Drain(&p);
    }

    // Let Clar process the message and exit.
    spins = 0;
    while (!Croi_Syscall_UserExited()) {
        Croi_Yield();
        if (++spins > 100000) {
            break;
        }
    }
    Croi_TaskSetSelfPriority(100);

    TEST_ASSERT(ctx, Croi_Syscall_UserExited(), "Clar exited");
    i64 status = Croi_Syscall_UserExitStatus();
    if (status != CLAR_EXIT_OK) {
        LOG_ERROR("clsm", "clar.elf exited with 0x%llx (expected 0x%x)", (u64)status, CLAR_EXIT_OK);
        TEST_FAIL(ctx, "Clar did not exit with CLAR_EXIT_OK");
    }

    // ---- Cleanup -----------------------------------------------------------
    Leargas_CloseScreen(scr);
    Dath_FreeBitmap(&save);
    Dath_FreeBitmap(&fb);
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);
}
