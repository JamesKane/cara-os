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
