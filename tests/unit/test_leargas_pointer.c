// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LA — pointer rendering.
// Exercises Init / Move with a synthetic 32×32 RGBA8888 framebuffer
// and a same-format 16×16 save buffer:
//
//   - Init paints the image and captures the underneath.
//   - Move restores the underneath at the old position, captures the
//     new underneath, and paints at the new position.
//   - Move that wraps off-screen clips correctly without corrupting
//     the framebuffer.
//   - Default arrow image has hot-spot (0, 0) and no out-of-range
//     pixel values.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_pointer: FAIL: %s\n", msg);
    return code;
}

// 32×32 RGBA8888 framebuffer; 4096 bytes.
static u32 g_fb_storage[32 * 32];
static u32 g_fb_baseline[32 * 32];
static u32 g_save_storage[16 * 16];

static void fb_init(struct DathFramebuffer *fb)
{
    fb->base = g_fb_storage;
    fb->width = 32;
    fb->height = 32;
    fb->stride = 32 * 4;
    fb->format = DATH_FMT_RGBA8888;
    fb->bpp = 4;
}

static void save_init(struct DathFramebuffer *save)
{
    save->base = g_save_storage;
    save->width = 16;
    save->height = 16;
    save->stride = 16 * 4;
    save->format = DATH_FMT_RGBA8888;
    save->bpp = 4;
}

// Paint the framebuffer with a recognisable per-pixel pattern, then
// snapshot it into g_fb_baseline so we can verify pixel-perfect
// restore after a Move.
static void paint_baseline(void)
{
    for (u32 y = 0; y < 32; y++) {
        for (u32 x = 0; x < 32; x++) {
            g_fb_storage[y * 32 + x] = 0xFF000000u | (x << 16) | (y << 8) | (u8)((x + y) & 0xFF);
        }
    }
    memcpy(g_fb_baseline, g_fb_storage, sizeof(g_fb_storage));
}

static bool fb_pixel_matches_baseline(u32 x, u32 y)
{
    return g_fb_storage[y * 32 + x] == g_fb_baseline[y * 32 + x];
}

int main(void)
{
    // Reject NULL / mismatched arguments.
    {
        struct LeargasPointer p;
        struct DathFramebuffer fb, save;
        fb_init(&fb);
        save_init(&save);
        if (Leargas_Pointer_Init(nullptr, &fb, &save, &leargas_pointer_arrow, 0, 0, 0, 0) !=
            CARA_EINVAL) {
            return fail("Init(NULL p) accepted", 1);
        }
        if (Leargas_Pointer_Init(&p, nullptr, &save, &leargas_pointer_arrow, 0, 0, 0, 0) !=
            CARA_EINVAL) {
            return fail("Init(NULL fb) accepted", 2);
        }
        if (Leargas_Pointer_Init(&p, &fb, nullptr, &leargas_pointer_arrow, 0, 0, 0, 0) !=
            CARA_EINVAL) {
            return fail("Init(NULL save) accepted", 3);
        }
        if (Leargas_Pointer_Init(&p, &fb, &save, nullptr, 0, 0, 0, 0) != CARA_EINVAL) {
            return fail("Init(NULL img) accepted", 4);
        }
    }

    // Save buffer smaller than image is rejected.
    {
        struct LeargasPointer p;
        struct DathFramebuffer fb, small;
        fb_init(&fb);
        u32 small_storage[8 * 8] = { 0 };
        small.base = small_storage;
        small.width = 8;
        small.height = 8;
        small.stride = 8 * 4;
        small.format = DATH_FMT_RGBA8888;
        small.bpp = 4;
        if (Leargas_Pointer_Init(&p, &fb, &small, &leargas_pointer_arrow, 0, 0, 16, 16) !=
            CARA_EINVAL) {
            return fail("Init accepted save buffer < image", 5);
        }
    }

    // Mismatched formats are rejected.
    {
        struct LeargasPointer p;
        struct DathFramebuffer fb, save;
        fb_init(&fb);
        save_init(&save);
        save.format = DATH_FMT_RGB565;
        save.bpp = 2;
        if (Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0, 0, 16, 16) !=
            CARA_EINVAL) {
            return fail("Init accepted format mismatch", 6);
        }
    }

    // Paint a baseline and Init the pointer at (8, 8). All pixels
    // outside the 16×16 image rect must remain at baseline; pixels
    // inside the rect are either baseline (transparent) or the
    // explicit fg/bg colour.
    paint_baseline();
    struct LeargasPointer p;
    struct DathFramebuffer fb, save;
    fb_init(&fb);
    save_init(&save);
    DathColor fg = 0xFFFFFFFFu; // white
    DathColor bg = 0xFF000000u; // black
    if (Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, fg, bg, 8, 8) != CARA_EOK) {
        return fail("Init returned non-EOK", 7);
    }
    if (!p.save_valid) {
        return fail("save_valid false after Init", 8);
    }
    if (p.x != 8 || p.y != 8) {
        return fail("position not stored", 9);
    }

    // Pixels outside the image rect [8..23] × [8..23] match baseline.
    for (u32 y = 0; y < 32; y++) {
        for (u32 x = 0; x < 32; x++) {
            if (x >= 8 && x < 24 && y >= 8 && y < 24) {
                continue;
            }
            if (!fb_pixel_matches_baseline(x, y)) {
                return fail("pixel outside image rect changed by Init", 10);
            }
        }
    }

    // At least one pixel inside the rect should equal `bg` (the
    // (0, 0) tip is a B in the arrow encoding).
    if (g_fb_storage[8 * 32 + 8] != bg) {
        return fail("Init did not paint tip pixel with bg colour", 11);
    }

    // Move from (8, 8) to (12, 14). The old position pixels must
    // restore to baseline; the new position must show pointer pixels.
    Leargas_Pointer_Move(&p, 12, 14);
    if (p.x != 12 || p.y != 14) {
        return fail("position not updated after Move", 12);
    }

    // The (8, 8) pixel must now be back to baseline (the tip moved
    // off it, so restore put baseline back).
    if (!fb_pixel_matches_baseline(8, 8)) {
        return fail("old tip pixel not restored after Move", 13);
    }

    // The new tip (12, 14) must be `bg`.
    if (g_fb_storage[14 * 32 + 12] != bg) {
        return fail("new tip pixel not painted after Move", 14);
    }

    // No-op move (same coords) must not corrupt anything.
    {
        u32 snap[32 * 32];
        memcpy(snap, g_fb_storage, sizeof(snap));
        Leargas_Pointer_Move(&p, 12, 14);
        if (memcmp(snap, g_fb_storage, sizeof(snap)) != 0) {
            return fail("no-op Move modified framebuffer", 15);
        }
    }

    // Move many times around the framebuffer; final framebuffer state
    // (after a final move back to a known location) must match the
    // baseline-with-pointer-at-known-spot. We verify that the off-
    // pointer area is unchanged.
    Leargas_Pointer_Move(&p, 0, 0);
    Leargas_Pointer_Move(&p, 31, 31);
    Leargas_Pointer_Move(&p, 16, 16);
    Leargas_Pointer_Move(&p, 8, 8);

    // Pixels outside [8..23]×[8..23] match baseline.
    for (u32 y = 0; y < 32; y++) {
        for (u32 x = 0; x < 32; x++) {
            if (x >= 8 && x < 24 && y >= 8 && y < 24) {
                continue;
            }
            if (!fb_pixel_matches_baseline(x, y)) {
                fprintf(stderr, "  pixel (%u,%u) drift: 0x%08x vs baseline 0x%08x\n", x, y,
                        g_fb_storage[y * 32 + x], g_fb_baseline[y * 32 + x]);
                return fail("post-trip pixel drift outside pointer rect", 16);
            }
        }
    }

    // Off-screen Move (negative coords) must not crash and must not
    // corrupt the part of the framebuffer it doesn't cover.
    {
        u32 snap[32 * 32];
        memcpy(snap, g_fb_storage, sizeof(snap));
        Leargas_Pointer_Move(&p, -100, -100);
        // The previously-painted rect at (8, 8) should be restored
        // to baseline; the off-screen move paints nothing.
        for (u32 y = 0; y < 32; y++) {
            for (u32 x = 0; x < 32; x++) {
                if (!fb_pixel_matches_baseline(x, y)) {
                    return fail("off-screen Move left painted pixels", 17);
                }
            }
        }
        // Move back into bounds. Should not crash and should paint
        // the pointer at the new location.
        Leargas_Pointer_Move(&p, 4, 4);
        if (g_fb_storage[4 * 32 + 4] != bg) {
            return fail("Move from off-screen back to (4,4) did not paint tip", 18);
        }
        (void)snap;
    }

    // Default arrow sanity: hot-spot (0, 0); only valid pixel values.
    if (leargas_pointer_arrow.hot_x != 0 || leargas_pointer_arrow.hot_y != 0) {
        return fail("default arrow hot-spot drifted from (0, 0)", 19);
    }
    if (leargas_pointer_arrow.width != 16 || leargas_pointer_arrow.height != 16) {
        return fail("default arrow dimensions drifted from 16×16", 20);
    }
    for (u32 i = 0; i < 16 * 16; i++) {
        u8 v = leargas_pointer_arrow.pixels[i];
        if (v > LEARGAS_PTR_BG) {
            return fail("default arrow contains out-of-range pixel value", 21);
        }
    }

    puts("leargas pointer ok");
    return 0;
}
