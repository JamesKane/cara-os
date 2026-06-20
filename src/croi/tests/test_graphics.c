// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(graphics_screen_rastport): L4.7 convergence — a Leargas
// screen now exposes a live graphics.library RastPort + BitMap over its
// framebuffer (Screen.RastPort / Screen.BitMap, previously nullptr), and
// drawing through it via the kernel-side graphics impls (Croi_Gfx_*_Impl)
// lands on the same screen surface the Leargas chrome renders to. This is
// the rendering convergence point window/app drawing builds on.

#include <cara/dath.h>
#include <cara/graphics_lib.h>
#include <cara/leargas.h>
#include <cara/test.h>
#include <cara/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/screens.h>

KERNEL_TEST(graphics_screen_rastport)
{
    static u32 pixels[16 * 8];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 16, 8, 16 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");

    struct Screen *scr = Leargas_OpenScreen(&fb, "gfxtest", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");
    TEST_ASSERT(ctx, scr->RastPort != nullptr, "Screen.RastPort is live");
    TEST_ASSERT(ctx, scr->BitMap != nullptr, "Screen.BitMap is live");
    TEST_ASSERT(ctx, scr->RastPort->BitMap == scr->BitMap, "RastPort.BitMap == Screen.BitMap");

    // Draw through the screen RastPort via the graphics impls; verify it
    // reaches the framebuffer pixels (OpenScreen cleared it to pen 0).
    struct RastPort *rp = scr->RastPort;
    Croi_Gfx_SetAPen_Impl(rp, 2); // palette red → 0xFFFF0000
    Croi_Gfx_RectFill_Impl(rp, 0, 0, 15, 7);
    TEST_ASSERT(ctx, pixels[0] == 0xFFFF0000u && pixels[16 * 8 - 1] == 0xFFFF0000u,
                "RectFill via Screen.RastPort hit the framebuffer");

    Croi_Gfx_SetAPen_Impl(rp, 1); // white
    TEST_ASSERT(ctx, Croi_Gfx_WritePixel_Impl(rp, 3, 2) == 0, "WritePixel ok");
    TEST_ASSERT(ctx, pixels[2 * 16 + 3] == 0xFFFFFFFFu, "WritePixel via Screen.RastPort");

    Leargas_CloseScreen(scr);
}
