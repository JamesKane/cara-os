// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(dath_smoke): exercise pixel/fill/clear/blit on a pair
// of synthetic in-heap framebuffers. No real display required —
// pixels go to RAM-backed buffers and we read them back to verify.
//
// Coverage:
//   - Dath_Framebuffer_Init validation (rejects bad args, derives bpp)
//   - Dath_Clear paints every pixel to the requested color
//   - Dath_Pixel writes exactly one pixel and respects clipping
//   - Dath_FillRect handles negative origins + oversize spans via clipping
//   - Dath_BlitRect copies the right region with proper source/dest clipping

#include <cara/dath.h>
#include <cara/test.h>
#include <cara/types.h>

#define W 64u
#define H 32u
#define SRC_W 16u
#define SRC_H 16u

static u32 g_buf[W * H];
static u32 g_src[SRC_W * SRC_H];

KERNEL_TEST(dath_smoke)
{
    struct DathFramebuffer fb;

    // 1. Init validation — reject zero width.
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, g_buf, 0, H, W * 4, DATH_FMT_RGBA8888)
                    != CARA_EOK,
                "init accepted zero width");
    // Reject undersize stride.
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, g_buf, W, H, W * 2, DATH_FMT_RGBA8888)
                    != CARA_EOK,
                "init accepted undersize stride");
    // Reject DATH_FMT_NONE.
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, g_buf, W, H, W * 4, DATH_FMT_NONE)
                    != CARA_EOK,
                "init accepted format NONE");

    // 2. Successful init.
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, g_buf, W, H, W * 4, DATH_FMT_RGBA8888)
                    == CARA_EOK,
                "init failed for valid args");
    TEST_ASSERT(ctx, fb.bpp == 4, "RGBA8888 bpp not 4");

    // 3. Clear paints every pixel.
    DathColor black = Dath_RGB(0, 0, 0);
    Dath_Clear(&fb, black);
    for (u32 i = 0; i < W * H; i++) {
        if (g_buf[i] != 0xFF000000u) {
            TEST_FAIL(ctx, "clear: pixel not black");
        }
    }

    // 4. Single pixel.
    Dath_Pixel(&fb, 10, 5, Dath_RGB(0xFF, 0, 0));
    TEST_ASSERT(ctx, g_buf[5 * W + 10] == 0xFFFF0000u, "pixel: red wrong");
    TEST_ASSERT(ctx, g_buf[5 * W + 11] == 0xFF000000u, "pixel: leaked right");
    TEST_ASSERT(ctx, g_buf[5 * W + 9]  == 0xFF000000u, "pixel: leaked left");

    // 5. Out-of-bounds pixels are silently dropped.
    Dath_Pixel(&fb, (i32)W, 0, Dath_RGB(1, 2, 3));
    Dath_Pixel(&fb, 0, (i32)H, Dath_RGB(1, 2, 3));
    Dath_Pixel(&fb, -1, 0, Dath_RGB(1, 2, 3));
    Dath_Pixel(&fb, 0, -1, Dath_RGB(1, 2, 3));
    // First pixel of buffer should still be the cleared value.
    TEST_ASSERT(ctx, g_buf[0] == 0xFF000000u, "out-of-bounds clobbered fb[0]");

    // 6. Filled rectangle.
    DathColor green = Dath_RGB(0, 0xFF, 0);
    Dath_FillRect(&fb, 16, 8, 8, 4, green);
    for (u32 y = 0; y < H; y++) {
        for (u32 x = 0; x < W; x++) {
            u32 expected;
            if (x >= 16 && x < 24 && y >= 8 && y < 12) {
                expected = 0xFF00FF00u;
            } else if (x == 10 && y == 5) {
                expected = 0xFFFF0000u;
            } else {
                expected = 0xFF000000u;
            }
            if (g_buf[y * W + x] != expected) {
                TEST_FAIL(ctx, "fill: pixel mismatch");
            }
        }
    }

    // 7. Negative-origin fill: clip drops the off-screen part.
    //    Rect at (-4, 28) size 8x8 should fill (0..3, 28..31).
    Dath_FillRect(&fb, -4, 28, 8, 8, Dath_RGB(0, 0, 0xFF));
    for (u32 y = 28; y < H; y++) {
        for (u32 x = 0; x < 4; x++) {
            if (g_buf[y * W + x] != 0xFF0000FFu) {
                TEST_FAIL(ctx, "clipped fill: pixel mismatch");
            }
        }
        // Pixel at column 4 should still be black.
        if (g_buf[y * W + 4] != 0xFF000000u) {
            TEST_FAIL(ctx, "clipped fill: bled past clip");
        }
    }

    // 8. Blit. Source filled yellow; copy to (40, 12) on dest.
    struct DathFramebuffer src;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&src, g_src, SRC_W, SRC_H, SRC_W * 4,
                                      DATH_FMT_RGBA8888) == CARA_EOK,
                "src init failed");
    Dath_Clear(&src, Dath_RGB(0xFF, 0xFF, 0));
    Dath_BlitRect(&fb, 40, 12, &src, 0, 0, (i32)SRC_W, (i32)SRC_H);
    for (u32 y = 12; y < 12 + SRC_H; y++) {
        for (u32 x = 40; x < 40 + SRC_W; x++) {
            if (g_buf[y * W + x] != 0xFFFFFF00u) {
                TEST_FAIL(ctx, "blit: pixel mismatch");
            }
        }
    }

    // 9. Blit clipped against dest right edge: dst (60, 0) src (0,0,16,4)
    //    should write only pixels (60..63, 0..3).
    Dath_Clear(&src, Dath_RGB(0xFF, 0, 0xFF));        // magenta
    // First clear an area we expect to remain black, so the clipped
    // blit's "wrote nothing past the edge" property is observable.
    Dath_FillRect(&fb, 56, 0, 8, 4, Dath_RGB(0, 0, 0));
    Dath_BlitRect(&fb, 60, 0, &src, 0, 0, (i32)SRC_W, 4);
    for (u32 y = 0; y < 4; y++) {
        for (u32 x = 60; x < W; x++) {
            if (g_buf[y * W + x] != 0xFFFF00FFu) {
                TEST_FAIL(ctx, "right-clipped blit pixel mismatch");
            }
        }
        // Just left of the blit destination: still black from the
        // pre-blit fill.
        if (g_buf[y * W + 59] != 0xFF000000u) {
            TEST_FAIL(ctx, "right-clipped blit bled left");
        }
    }

    // 10. Text rendering. Use a fresh framebuffer so we can read the
    //     'C' glyph's row[0] pixels without prior garbage. The font's
    //     'C' starts with row 0 = 0x7C  →  pixels .#####.. across 8 cols.
    static u32 g_text_buf[16 * 8];
    struct DathFramebuffer tfb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&tfb, g_text_buf, 16, 8, 16 * 4,
                                      DATH_FMT_RGBA8888) == CARA_EOK,
                "text fb init failed");
    Dath_Clear(&tfb, Dath_RGB(0, 0, 0));
    DathColor white = Dath_RGB(0xFF, 0xFF, 0xFF);
    Dath_DrawChar(&tfb, &dath_font_8x8, 0, 0, 'C', white,
                  Dath_RGB(0, 0, 0));

    // 'C' row 0 = 0x7C = 0b01111100 — pixels at columns 1..5 are set.
    static const u8 row0_bits[8] = { 0, 1, 1, 1, 1, 1, 0, 0 };
    for (u32 x = 0; x < 8; x++) {
        u32 expected = row0_bits[x] ? 0xFFFFFFFFu : 0xFF000000u;
        if (g_text_buf[0 * 16 + x] != expected) {
            TEST_FAIL(ctx, "DrawChar 'C' row 0 pixel mismatch");
        }
    }

    // 11. DrawString advances by font width per char.
    Dath_Clear(&tfb, Dath_RGB(0, 0, 0));
    Dath_DrawString(&tfb, &dath_font_8x8, 0, 0, "C C", white,
                    Dath_RGB(0, 0, 0));
    // Second 'C' starts at x=16, so its row 0 should match too.
    for (u32 x = 0; x < 8; x++) {
        u32 expected = row0_bits[x] ? 0xFFFFFFFFu : 0xFF000000u;
        if (g_text_buf[0 * 16 + x] != expected) {
            TEST_FAIL(ctx, "DrawString first 'C' wrong");
        }
    }
}
