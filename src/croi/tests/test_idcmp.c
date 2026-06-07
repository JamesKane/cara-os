// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(idcmp_rawkey): the LF keyboard-routing path end-to-end
// through the real per-window IDCMP MsgPort. Opens a screen + a window
// that requested IDCMP_RAWKEY, confirms it got a UserPort and focus,
// then checks both direct delivery (Leargas_IDCMP_RouteKey) and the
// full ring path (Leargas_Input_Post -> Leargas_Input_Drain -> hook).
// Also covers the rejection cases. Runs after Sched_Init so there is a
// current task to own the port + signal.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/shared.h>
#include <cara/test.h>
#include <cara/types.h>
#include <devices/inputevent.h>

// A Window / IntuiMessage a U-mode client will dereference must live in
// the SASOS shared window (S3), not the upper-half kernel heap.
static bool in_shared(const void *p)
{
    u64 va = (u64)(uptr)p;
    return va >= CARA_SHARED_VA_BASE && va < CARA_SHARED_VA_BASE + CARA_SHARED_ARENA_BYTES;
}

KERNEL_TEST(idcmp_rawkey)
{
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Screen_SetActive(nullptr);
    Leargas_SetKeyRouter(Leargas_IDCMP_RouteKey);

    struct DathFramebuffer fb;
    TEST_ASSERT(ctx, Dath_AllocBitmap(&fb, 200, 120, DATH_FMT_RGBA8888) == CARA_EOK,
                "framebuffer alloc");

    struct Screen *scr = Leargas_OpenScreen(&fb, "T", Dath_RGB(0x10, 0x10, 0x20));
    TEST_ASSERT(ctx, scr != nullptr, "open screen");

    char wtitle[] = "Win";
    struct NewWindow nw = {
        .LeftEdge = 10,
        .TopEdge = 10,
        .Width = 120,
        .Height = 70,
        .Flags = WFLG_DRAGBAR | WFLG_CLOSEGADGET | WFLG_ACTIVATE,
        .IDCMPFlags = IDCMP_RAWKEY | IDCMP_CLOSEWINDOW,
        .Title = (UBYTE *)wtitle,
        .Screen = scr,
    };
    struct Window *win = Leargas_OpenWindow(&nw);
    TEST_ASSERT(ctx, win != nullptr, "open window");
    TEST_ASSERT(ctx, in_shared(win), "Window allocated in the SASOS shared window (S3)");
    TEST_ASSERT(ctx, win->UserPort != nullptr, "window got a UserPort");
    TEST_ASSERT(ctx, Leargas_ActiveWindow() == win, "WFLG_ACTIVATE focused the window");

    // ---- Direct delivery: RouteKey -> UserPort -----------------------------
    struct LeargasInputEvent k = {
        .ie_class = IECLASS_RAWKEY,
        .ie_code = 0x20, // 'A'
        .ie_qualifier = IEQUALIFIER_LSHIFT,
    };
    TEST_ASSERT(ctx, Leargas_IDCMP_RouteKey(win, &k), "RouteKey delivered");

    struct IntuiMessage *im = nullptr;
    TEST_ASSERT(ctx, Leargas_IDCMP_GetMsg(win, &im), "GetMsg returned a message");
    TEST_ASSERT(ctx, im != nullptr, "message non-null");
    TEST_ASSERT(ctx, in_shared(im), "IntuiMessage allocated in the shared window (S3)");
    TEST_ASSERT(ctx, im->Class == IDCMP_RAWKEY, "Class IDCMP_RAWKEY");
    TEST_ASSERT(ctx, im->Code == 0x20, "Code");
    TEST_ASSERT(ctx, im->Qualifier == IEQUALIFIER_LSHIFT, "Qualifier");
    TEST_ASSERT(ctx, im->IDCMPWindow == win, "IDCMPWindow back-pointer");
    Leargas_IDCMP_DisposeMsg(im);
    TEST_ASSERT(ctx, !Leargas_IDCMP_GetMsg(win, &im), "port empty after drain");

    // ---- Rejection: a window that didn't ask for RAWKEY --------------------
    char w2title[] = "NoKeys";
    struct NewWindow nw2 = nw;
    nw2.Title = (UBYTE *)w2title;
    nw2.LeftEdge = 60;
    nw2.IDCMPFlags = IDCMP_CLOSEWINDOW; // a port, but no RAWKEY
    nw2.Flags = WFLG_DRAGBAR;           // not ACTIVATE — focus stays on win
    struct Window *win2 = Leargas_OpenWindow(&nw2);
    TEST_ASSERT(ctx, win2 != nullptr, "open window2");
    TEST_ASSERT(ctx, win2->UserPort != nullptr, "window2 got a UserPort");
    TEST_ASSERT(ctx, Leargas_ActiveWindow() == win, "opening win2 didn't steal focus");
    TEST_ASSERT(ctx, !Leargas_IDCMP_RouteKey(win2, &k), "RouteKey refused (no IDCMP_RAWKEY)");
    TEST_ASSERT(ctx, !Leargas_IDCMP_GetMsg(win2, &im), "window2 port stayed empty");
    TEST_ASSERT(ctx, !Leargas_IDCMP_RouteKey(nullptr, &k), "RouteKey(null window) refused");

    // ---- Full ring path: Post -> Drain -> hook -> focused window -----------
    struct DathFramebuffer save;
    TEST_ASSERT(ctx,
                Dath_AllocBitmap(&save, leargas_pointer_arrow.width, leargas_pointer_arrow.height,
                                 fb.format) == CARA_EOK,
                "pointer save alloc");
    struct LeargasPointer p;
    TEST_ASSERT(ctx,
                Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow,
                                     Dath_RGB(0xFF, 0xFF, 0xFF), Dath_RGB(0, 0, 0), 60,
                                     40) == CARA_EOK,
                "pointer init");

    struct LeargasInputEvent ke = {
        .ie_class = IECLASS_RAWKEY,
        .ie_code = 0x21, // 'S'
        .ie_qualifier = 0,
    };
    TEST_ASSERT(ctx, Leargas_Input_Post(&ke), "post RAWKEY to L0 ring");
    (void)Leargas_Input_Drain(&p);
    TEST_ASSERT(ctx, Leargas_IDCMP_GetMsg(win, &im), "drained key reached focused window");
    TEST_ASSERT(ctx, im->Code == 0x21, "drained key Code");
    Leargas_IDCMP_DisposeMsg(im);

    // ---- Cleanup -----------------------------------------------------------
    Leargas_CloseWindow(win);
    Leargas_CloseWindow(win2);
    Leargas_CloseScreen(scr);
    Dath_FreeBitmap(&save);
    Dath_FreeBitmap(&fb);
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Screen_SetActive(nullptr);
}
