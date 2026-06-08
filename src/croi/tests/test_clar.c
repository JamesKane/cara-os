// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(clar_smoke): drives the Clar Workbench Gleas end-to-end —
// the Phase 1 success criterion in automated form. The test plays the
// input source (the live boot path uses a kernel input-pump fed by HID):
// it stands up a screen, spawns Clar as a U-mode task, then injects, via
// the real input ring + router:
//   1. a click on the drawer  -> Clar opens the child "Bosca" window with
//      a string Inntin and activates it,
//   2. a few keystrokes + Return -> the Inntin captures the text and Clar
//      logs it (IDCMP_GADGETUP),
//   3. a click on the desktop close gadget -> Clar quits.
// Clar returns CLAR_EXIT_OK only if it actually received typed text, so
// asserting the exit status proves the whole stack.
//
// Concurrency: Clar runs above the test's priority, so after we drop ours
// Clar gets the CPU, sets up, and blocks in Wait, handing control back to
// us to inject the next step.

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

// Screen-absolute coordinates, matching the geometry in clar.c:
//   desktop window (10,10) 160x90, BorderTop 11
//   drawer gadget window-rel (20,30) 60x20 -> centre absolute (60,50)
//   desktop close box window-rel [149,160)x[0,11) -> absolute ~(164,15)
#define CLAR_DRAWER_ABS_X 60
#define CLAR_DRAWER_ABS_Y 50
#define CLAR_CLOSE_ABS_X 164
#define CLAR_CLOSE_ABS_Y 15

static void post_button(struct LeargasPointer *p, u16 code)
{
    struct LeargasInputEvent ev = { .ie_class = IECLASS_RAWMOUSE, .ie_code = code };
    (void)Leargas_Input_Post(&ev);
    (void)Leargas_Input_Drain(p);
}

static void post_motion(struct LeargasPointer *p, i16 dx, i16 dy)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWMOUSE, .ie_code = IECODE_NOBUTTON, .ie_dx = dx, .ie_dy = dy
    };
    (void)Leargas_Input_Post(&ev);
    (void)Leargas_Input_Drain(p);
}

static void post_key(struct LeargasPointer *p, u16 rawkey)
{
    struct LeargasInputEvent ev = { .ie_class = IECLASS_RAWKEY, .ie_code = rawkey };
    (void)Leargas_Input_Post(&ev);
    (void)Leargas_Input_Drain(p);
}

// Yield until `pred` (passed as a 0-arg lambda-ish expression) — spelled
// out inline at call sites; this helper just bounds the spin.
static bool wait_until_window_changed(struct Window *was, int limit)
{
    for (int i = 0; i < limit; i++) {
        if (Croi_Syscall_UserExited()) {
            return false;
        }
        if (Leargas_ActiveWindow() != was) {
            return true;
        }
        Croi_Yield();
    }
    return false;
}

KERNEL_TEST(clar_smoke)
{
    Croi_Syscall_ResetUserExit();
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);
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

    Croi_TaskSetSelfPriority(-1);

    // Wait for Clar's desktop window + drawer gadget.
    int spins = 0;
    while (!Croi_Syscall_UserExited()) {
        struct Window *aw = Leargas_ActiveWindow();
        if (aw && aw->FirstGadget) {
            break;
        }
        Croi_Yield();
        if (++spins > 100000) {
            break;
        }
    }
    struct Window *desktop = Leargas_ActiveWindow();
    TEST_ASSERT(ctx, !Croi_Syscall_UserExited() && desktop && desktop->FirstGadget,
                "Clar opened its desktop window + drawer gadget");

    // 1. Click the drawer (pointer already at its centre). The release
    //    posts IDCMP_GADGETUP; Clar opens the child window + Inntin and
    //    activates it (the active window flips from desktop to child).
    post_button(&p, IECODE_LBUTTON);
    post_button(&p, (u16)(IECODE_LBUTTON | IECODE_UP_PREFIX));
    TEST_ASSERT(ctx, wait_until_window_changed(desktop, 100000),
                "drawer click opened the child Bosca window");

    // 2. Type into the now-active Inntin, then Return. The keys route to
    //    the active string gadget; Return posts IDCMP_GADGETUP with the
    //    buffer contents, which Clar reads + logs.
    post_key(&p, 0x20); // 'a'
    post_key(&p, 0x21); // 's'
    post_key(&p, 0x44); // Return -> commit

    // 3. Move to the desktop close gadget and click it to quit Clar.
    post_motion(&p, (i16)(CLAR_CLOSE_ABS_X - CLAR_DRAWER_ABS_X),
                (i16)(CLAR_CLOSE_ABS_Y - CLAR_DRAWER_ABS_Y));
    post_button(&p, IECODE_LBUTTON);
    post_button(&p, (u16)(IECODE_LBUTTON | IECODE_UP_PREFIX));

    // Let Clar process everything and exit.
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
        TEST_FAIL(ctx, "Clar did not exit with CLAR_EXIT_OK (did the typed text arrive?)");
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
