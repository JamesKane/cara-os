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

// L12.1: graphics.library Text now renders from the bound TextFont's
// strike (tf_CharData/tf_CharLoc), not the hardwired 8x8. This pins the
// strike path bit-for-bit to the source dath_font_8x8: render a glyph and
// compare every pixel to the glyph's own bitmap. A pure refactor — the ROM
// face must draw exactly as before.
KERNEL_TEST(graphics_text_strike)
{
    static u32 pixels[8 * 8];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx, Dath_Framebuffer_Init(&fb, pixels, 8, 8, 8 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "txt", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");
    struct RastPort *rp = scr->RastPort;

    struct TextFont *f = Croi_Gfx_OpenFont_Impl(nullptr);
    TEST_ASSERT(ctx, f != nullptr, "OpenFont");
    TEST_ASSERT(ctx, f->tf_XSize == 8 && f->tf_YSize == 8, "system font metrics");
    TEST_ASSERT(ctx, f->tf_CharData != nullptr && f->tf_CharLoc != nullptr, "font has a strike");
    Croi_Gfx_SetFont_Impl(rp, f);

    // Monospace: TextLength is count * width.
    TEST_ASSERT(ctx, Croi_Gfx_TextLength_Impl(rp, (STRPTR) "AB", 2) == 16, "TextLength monospace");

    Croi_Gfx_SetAPen_Impl(rp, 1); // FgPen white 0xFFFFFFFF
    rp->cp_x = 0;
    rp->cp_y = 0;
    char s[1] = { 'A' };
    Croi_Gfx_Text_Impl(rp, s, 1);
    TEST_ASSERT(ctx, rp->cp_x == 8, "cursor advanced by one glyph");

    // Compare to the source glyph: set bits → FgPen, clear bits → not FgPen.
    const u8 *glyph = dath_font_8x8.bitmap + ((u32)'A' - dath_font_8x8.first_glyph) * 8u;
    bool match = true;
    for (u32 r = 0; r < 8; r++) {
        for (u32 c = 0; c < 8; c++) {
            bool set = (glyph[r] >> (7u - c)) & 1u;
            u32 px = pixels[r * 8 + c];
            if (set ? (px != 0xFFFFFFFFu) : (px == 0xFFFFFFFFu)) {
                match = false;
            }
        }
    }
    TEST_ASSERT(ctx, match, "strike render matches dath_font_8x8 glyph");

    Leargas_CloseScreen(scr);
}
