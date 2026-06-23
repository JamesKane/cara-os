// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(diskfont_render): diskfont.library's Cara font codec + the
// disk-font render path (L12.2), driven without dos. We parse a hand-built
// Cara font file (a 1-glyph 'A' box, §2.2) with Croi_Diskfont_ParseFont,
// then SetFont + Text it through a Leargas screen RastPort and assert the
// rendered pixels match the encoded strike — proving a disk-loaded TextFont
// draws via the L12.1 generic-strike renderer. The FONTS: dos path is
// exercised separately by the userexec Gleas round-trip (OpenDiskFont).

#include <cara/dath.h>
#include <cara/diskfont_lib.h>
#include <cara/graphics_lib.h>
#include <cara/leargas.h>
#include <cara/test.h>
#include <cara/types.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/screens.h>

// A Cara font file: one glyph 'A', 8x8, monospace. The glyph is a box so
// the rendered shape is unambiguous. Header (22) + strike (modulo 2 * 8 =
// 16) + charLoc (2 glyphs * 4). See cara/diskfont_lib.h §2.2.
static const u8 cfnt_box[] = {
    'C',
    'F',
    'N',
    'T', // magic
    0x01,
    0x00, // version 1
    0x00,
    0x00, // flags (monospace)
    0x08,
    0x00, // ySize 8
    0x08,
    0x00, // xSize 8
    0x07,
    0x00, // baseline 7
    0x00,
    0x00, // boldSmear
    0x02,
    0x00, // modulo 2
    0x00,
    0x00, // style
    0x41,
    0x41, // loChar 'A', hiChar 'A'
    // strike: 2 bytes/row, byte0 = the 'A' glyph (box), byte1 = blank default
    0xFF,
    0x00,
    0x81,
    0x00,
    0x81,
    0x00,
    0x81,
    0x00,
    0x81,
    0x00,
    0x81,
    0x00,
    0x81,
    0x00,
    0xFF,
    0x00,
    // charLoc[0] = (0<<16)|8, charLoc[1] (default) = (8<<16)|8
    0x08,
    0x00,
    0x00,
    0x00,
    0x08,
    0x00,
    0x08,
    0x00,
};

static const u8 box_rows[8] = { 0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF };

KERNEL_TEST(diskfont_render)
{
    struct TextFont *f = Croi_Diskfont_ParseFont(cfnt_box, sizeof cfnt_box);
    TEST_ASSERT(ctx, f != nullptr, "parse Cara font");
    TEST_ASSERT(ctx, f->tf_YSize == 8 && f->tf_XSize == 8, "font metrics");
    TEST_ASSERT(ctx, f->tf_LoChar == 'A' && f->tf_HiChar == 'A', "char range");
    TEST_ASSERT(ctx, (f->tf_Flags & FPF_DISKFONT) != 0, "FPF_DISKFONT set");
    TEST_ASSERT(ctx, f->tf_CharData != nullptr && f->tf_Modulo == 2, "strike present");
    TEST_ASSERT(ctx, ((const u8 *)f->tf_CharData)[0] == 0xFF, "strike first byte");

    static u32 pixels[8 * 8];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx, Dath_Framebuffer_Init(&fb, pixels, 8, 8, 8 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "df", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");
    struct RastPort *rp = scr->RastPort;

    Croi_Gfx_SetFont_Impl(rp, f);
    Croi_Gfx_SetAPen_Impl(rp, 1); // FgPen white
    rp->cp_x = 0;
    rp->cp_y = 0;
    char s[1] = { 'A' };
    Croi_Gfx_Text_Impl(rp, s, 1);
    TEST_ASSERT(ctx, rp->cp_x == 8, "cursor advanced");

    bool match = true;
    for (u32 r = 0; r < 8; r++) {
        for (u32 c = 0; c < 8; c++) {
            bool set = (box_rows[r] >> (7u - c)) & 1u;
            u32 px = pixels[r * 8 + c];
            if (set ? (px != 0xFFFFFFFFu) : (px == 0xFFFFFFFFu)) {
                match = false;
            }
        }
    }
    TEST_ASSERT(ctx, match, "disk font glyph rendered");

    Leargas_CloseScreen(scr);
    Croi_Gfx_CloseFont_Impl(f); // frees the FPF_DISKFONT allocation
}
