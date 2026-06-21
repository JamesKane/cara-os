// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(intuition_openwindowtaglist): L5.1 — the tag window opener
// + the window content RPort. Opens a screen over an off-screen
// framebuffer, opens a window via Croi_OpenWindowTagList_Impl (WA_* tags),
// and proves: the tags applied, window->RPort is a live graphics.library
// RastPort, and drawing into it (window-relative, via the gfx impls)
// lands at the window's content origin on the screen (the "sub-bitmap"
// view, docs/LEARGAS_INTUITION.md §2.2). Plus ModifyIDCMP.

#include <cara/dath.h>
#include <cara/graphics_lib.h>
#include <cara/intuition_lib.h>
#include <cara/leargas.h>
#include <cara/test.h>
#include <cara/types.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>

KERNEL_TEST(intuition_openwindowtaglist)
{
    static u32 pixels[64 * 48];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 64, 48, 64 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "wb", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");

    struct TagItem tags[] = {
        { WA_Left, 4 },
        { WA_Top, 4 },
        { WA_Width, 40 },
        { WA_Height, 30 },
        { WA_DragBar, 1 },
        { WA_Activate, 1 },
        { WA_Title, (IPTR)(uptr) "T" },
        { TAG_DONE, 0 },
    };
    struct Window *win = Croi_OpenWindowTagList_Impl(nullptr, tags);
    TEST_ASSERT(ctx, win != nullptr, "OpenWindowTagList");
    TEST_ASSERT(ctx, win->LeftEdge == 4 && win->Width == 40 && win->Height == 30, "WA_* applied");
    TEST_ASSERT(ctx, (win->Flags & WFLG_DRAGBAR) != 0, "WA_DragBar → WFLG_DRAGBAR");
    TEST_ASSERT(ctx, win->RPort != nullptr && win->RPort->BitMap != nullptr, "window RPort live");

    // Draw into the window RPort at window-relative (0,0); it must land at
    // the window's content origin (LeftEdge+BorderLeft, TopEdge+BorderTop)
    // on the screen framebuffer.
    struct RastPort *rp = win->RPort;
    Croi_Gfx_SetAPen_Impl(rp, 2); // palette red → 0xFFFF0000
    TEST_ASSERT(ctx, Croi_Gfx_WritePixel_Impl(rp, 0, 0) == 0, "WritePixel ok");
    i32 ox = win->LeftEdge + win->BorderLeft;
    i32 oy = win->TopEdge + win->BorderTop;
    TEST_ASSERT(ctx, pixels[(u32)oy * 64 + (u32)ox] == 0xFFFF0000u,
                "RPort(0,0) hit the window content origin");

    // A write past the content area is clipped (the sub-bitmap is sized
    // to the content, so x == content width is out of bounds → -1).
    i32 cw = win->Width - win->BorderLeft - win->BorderRight;
    TEST_ASSERT(ctx, Croi_Gfx_WritePixel_Impl(rp, (WORD)cw, 0) == -1, "RPort clips to window");

    Croi_ModifyIDCMP_Impl(win, IDCMP_CLOSEWINDOW);
    TEST_ASSERT(ctx, win->IDCMPFlags == IDCMP_CLOSEWINDOW, "ModifyIDCMP");

    Leargas_CloseWindow(win);
    Leargas_CloseScreen(scr);
}

// L5.2 — window ops + activation.
KERNEL_TEST(intuition_window_ops)
{
    static u32 pixels[64 * 48];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 64, 48, 64 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "wb", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");

    struct TagItem t1[] = {
        { WA_Left, 4 },    { WA_Top, 4 },     { WA_Width, 30 },
        { WA_Height, 24 }, { WA_DragBar, 1 }, { TAG_DONE, 0 },
    };
    struct Window *w1 = Croi_OpenWindowTagList_Impl(nullptr, t1);
    TEST_ASSERT(ctx, w1 != nullptr, "open w1");

    // MoveWindow updates geometry and recomputes the RPort sub-bitmap.
    Croi_MoveWindow_Impl(w1, 6, 3);
    TEST_ASSERT(ctx, w1->LeftEdge == 10 && w1->TopEdge == 7, "MoveWindow geometry");
    Croi_Gfx_SetAPen_Impl(w1->RPort, 2); // red
    Croi_Gfx_WritePixel_Impl(w1->RPort, 0, 0);
    i32 ox = w1->LeftEdge + w1->BorderLeft;
    i32 oy = w1->TopEdge + w1->BorderTop;
    TEST_ASSERT(ctx, pixels[(u32)oy * 64 + (u32)ox] == 0xFFFF0000u, "RPort recomputed after move");

    // SizeWindow adjusts by the delta.
    WORD ow = w1->Width;
    WORD oh = w1->Height;
    Croi_SizeWindow_Impl(w1, 8, 4);
    TEST_ASSERT(ctx, w1->Width == ow + 8 && w1->Height == oh + 4, "SizeWindow");

    // SetWindowTitles (screen title unchanged via the -1 sentinel).
    Croi_SetWindowTitles_Impl(w1, (STRPTR) "New", (STRPTR)(uptr)-1);
    TEST_ASSERT(ctx, w1->Title != nullptr && w1->Title[0] == 'N', "SetWindowTitles");

    // ActivateWindow sets WFLG_WINDOWACTIVE.
    Croi_ActivateWindow_Impl(w1);
    TEST_ASSERT(ctx, (w1->Flags & WFLG_WINDOWACTIVE) != 0, "ActivateWindow");

    // Front/back reorder the screen window list (open a 2nd window first;
    // it head-inserts, so it starts front-most).
    struct TagItem t2[] = {
        { WA_Left, 20 }, { WA_Top, 20 }, { WA_Width, 20 }, { WA_Height, 16 }, { TAG_DONE, 0 },
    };
    struct Window *w2 = Croi_OpenWindowTagList_Impl(nullptr, t2);
    TEST_ASSERT(ctx, w2 != nullptr && scr->FirstWindow == w2, "w2 front-most on open");
    Croi_WindowToFront_Impl(w1);
    TEST_ASSERT(ctx, scr->FirstWindow == w1, "WindowToFront");
    Croi_WindowToBack_Impl(w1);
    TEST_ASSERT(ctx, scr->FirstWindow == w2, "WindowToBack");

    Leargas_CloseWindow(w2);
    Leargas_CloseWindow(w1);
    Leargas_CloseScreen(scr);
}

// L5.3 — menus: SetMenuStrip layout + ItemAddress + the menu-button
// pick path (Begin/End) + IDCMP_MENUPICK delivery.
KERNEL_TEST(intuition_menus)
{
    static u32 pixels[80 * 60];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 80, 60, 80 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "wb", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");

    // A menu strip: one menu "Project" with items "New", "Quit".
    static char nm_project[] = "Project";
    static char nm_new[] = "New";
    static char nm_quit[] = "Quit";
    static struct IntuiText it_new = { .IText = (UBYTE *)nm_new, .DrawMode = JAM2 };
    static struct IntuiText it_quit = { .IText = (UBYTE *)nm_quit, .DrawMode = JAM2 };
    static struct MenuItem mi_quit = { .NextItem = nullptr,
                                       .Flags = ITEMTEXT | ITEMENABLED,
                                       .ItemFill = &it_quit };
    static struct MenuItem mi_new = { .NextItem = &mi_quit,
                                      .Flags = ITEMTEXT | ITEMENABLED,
                                      .ItemFill = &it_new };
    static struct Menu menu = { .NextMenu = nullptr,
                                .Flags = MENUENABLED,
                                .MenuName = (BYTE *)nm_project,
                                .FirstItem = &mi_new };

    struct TagItem tags[] = {
        { WA_Left, 2 },
        { WA_Top, 2 },
        { WA_Width, 40 },
        { WA_Height, 30 },
        { WA_IDCMP, IDCMP_MENUPICK },
        { WA_Activate, 1 },
        { TAG_DONE, 0 },
    };
    struct Window *win = Croi_OpenWindowTagList_Impl(nullptr, tags);
    TEST_ASSERT(ctx, win != nullptr && win->UserPort != nullptr, "open window w/ MENUPICK port");

    Croi_SetMenuStrip_Impl(win, &menu);
    TEST_ASSERT(ctx, win->MenuStrip == &menu, "SetMenuStrip");
    TEST_ASSERT(ctx, menu.Width > 0 && mi_new.Height > 0 && mi_quit.TopEdge > mi_new.TopEdge,
                "layout filled geometry");

    // ItemAddress resolves the packed number → the 2nd item.
    u16 code_quit = (u16)FULLMENUNUM(0, 1, NOSUB);
    TEST_ASSERT(ctx, Croi_ItemAddress_Impl(&menu, code_quit) == &mi_quit, "ItemAddress");

    // Menu-button pick: Begin (RMB-down), then End at the laid-out box of
    // "Quit" (RMB-up) → its packed code.
    TEST_ASSERT(ctx, Leargas_Menu_Begin(win), "Menu_Begin");
    struct Window *mw = nullptr;
    u16 code = 0;
    i32 px = mi_quit.LeftEdge + 2;
    i32 py = mi_quit.TopEdge + 1;
    TEST_ASSERT(ctx, Leargas_Menu_End(px, py, &mw, &code), "Menu_End");
    TEST_ASSERT(ctx, mw == win && code == code_quit, "pick resolves to Quit");

    // Deliver IDCMP_MENUPICK and read it back from the UserPort.
    TEST_ASSERT(ctx, Leargas_IDCMP_PostMenuPick(win, code), "PostMenuPick");
    struct IntuiMessage *im = nullptr;
    TEST_ASSERT(ctx, Leargas_IDCMP_GetMsg(win, &im), "GetMsg");
    TEST_ASSERT(ctx, im->Class == IDCMP_MENUPICK && im->Code == code_quit, "MENUPICK delivered");
    Leargas_IDCMP_DisposeMsg(im);

    Croi_ClearMenuStrip_Impl(win);
    TEST_ASSERT(ctx, win->MenuStrip == nullptr, "ClearMenuStrip");

    Leargas_CloseWindow(win);
    Leargas_CloseScreen(scr);
}

// L5.4 — feedback / timing helpers.
KERNEL_TEST(intuition_feedback)
{
    // CurrentTime normalises micros to < 1e6.
    ULONG s = 0;
    ULONG us = 999999999u;
    Croi_CurrentTime_Impl(&s, &us);
    TEST_ASSERT(ctx, us < 1000000u, "CurrentTime micros normalised");

    // DoubleClick: within 0.5 s → TRUE; beyond or negative → FALSE.
    TEST_ASSERT(ctx, Croi_DoubleClick_Impl(0, 0, 0, 100000) == TRUE, "doubleclick within");
    TEST_ASSERT(ctx, Croi_DoubleClick_Impl(0, 0, 1, 0) == FALSE, "doubleclick beyond");
    TEST_ASSERT(ctx, Croi_DoubleClick_Impl(5, 0, 4, 0) == FALSE, "doubleclick negative");

    Croi_DisplayBeep_Impl(nullptr); // must not crash with no screen

    // ReportMouse toggles a window's mouse-move reporting.
    static u32 pixels[32 * 24];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 32, 24, 32 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "wb", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");
    struct TagItem tags[] = {
        { WA_Left, 1 }, { WA_Top, 1 }, { WA_Width, 20 }, { WA_Height, 16 }, { TAG_DONE, 0 },
    };
    struct Window *win = Croi_OpenWindowTagList_Impl(nullptr, tags);
    TEST_ASSERT(ctx, win != nullptr, "open window");
    Croi_ReportMouse_Impl(TRUE, win);
    TEST_ASSERT(ctx, (win->Flags & WFLG_REPORTMOUSE) && (win->IDCMPFlags & IDCMP_MOUSEMOVE),
                "ReportMouse on");
    Croi_ReportMouse_Impl(FALSE, win);
    TEST_ASSERT(ctx, !(win->Flags & WFLG_REPORTMOUSE) && !(win->IDCMPFlags & IDCMP_MOUSEMOVE),
                "ReportMouse off");
    Leargas_CloseWindow(win);
    Leargas_CloseScreen(scr);
}
