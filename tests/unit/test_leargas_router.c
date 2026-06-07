// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LC — mouse-motion router.
// Posts synthetic IECLASS_RAWMOUSE events through the L0 ring,
// drains via Leargas_Input_Drain, and verifies pointer position
// advancement, clamping against the active screen, and skip-over
// of non-RAWMOUSE events.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <devices/inputevent.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_router: FAIL: %s\n", msg);
    return code;
}

static u32 g_fb_storage[64 * 48];
static u32 g_save_storage[16 * 16];

static void fb_init(struct DathFramebuffer *fb)
{
    fb->base = g_fb_storage;
    fb->width = 64;
    fb->height = 48;
    fb->stride = 64 * 4;
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

static void post_mouse(i16 dx, i16 dy)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = IECODE_NOBUTTON,
        .ie_qualifier = IEQUALIFIER_RELATIVEMOUSE,
        .ie_dx = dx,
        .ie_dy = dy,
        .ie_ts_ns = 0,
    };
    (void)Leargas_Input_Post(&ev);
}

static void post_key(u16 rawkey)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWKEY,
        .ie_code = rawkey,
        .ie_qualifier = 0,
        .ie_ts_ns = 0,
    };
    (void)Leargas_Input_Post(&ev);
}

int main(void)
{
    Leargas_Input_Reset();
    Leargas_Screen_SetActive(nullptr);

    // Drain on a fresh ring is a no-op.
    {
        struct DathFramebuffer fb, save;
        struct LeargasPointer p;
        fb_init(&fb);
        save_init(&save);
        if (Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0xFFFFFFFFu, 0xFF000000u,
                                 16, 16) != CARA_EOK) {
            return fail("pointer init failed", 1);
        }
        if (Leargas_Input_Drain(&p) != 0) {
            return fail("Drain on empty ring returned non-zero", 2);
        }
        if (p.x != 16 || p.y != 16) {
            return fail("Drain on empty ring moved pointer", 3);
        }
    }

    // Drain(NULL) is a safe no-op.
    if (Leargas_Input_Drain(nullptr) != 0) {
        return fail("Drain(NULL) returned non-zero", 4);
    }

    // Without an active screen, deltas accumulate without clamping.
    {
        struct DathFramebuffer fb, save;
        struct LeargasPointer p;
        fb_init(&fb);
        save_init(&save);
        Leargas_Input_Reset();
        Leargas_Screen_SetActive(nullptr);
        if (Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0xFFFFFFFFu, 0xFF000000u,
                                 16, 16) != CARA_EOK) {
            return fail("pointer init failed (no-screen scenario)", 5);
        }
        post_mouse(5, 7);
        post_mouse(-2, 1);
        u32 n = Leargas_Input_Drain(&p);
        if (n != 2) {
            return fail("expected 2 events drained without screen", 6);
        }
        if (p.x != 16 + 5 - 2 || p.y != 16 + 7 + 1) {
            return fail("pointer position wrong without screen clamp", 7);
        }
    }

    // With an active screen, clamp at edges.
    {
        struct DathFramebuffer fb, save;
        struct LeargasPointer p;
        struct LeargasScreen screen;
        fb_init(&fb);
        save_init(&save);
        Leargas_Input_Reset();
        if (Leargas_Screen_InitInPlace(&screen, &fb, "TestScreen", 0xFF101020u) != CARA_EOK) {
            return fail("screen init failed", 8);
        }
        Leargas_Screen_SetActive(&screen);
        if (Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0xFFFFFFFFu, 0xFF000000u,
                                 32, 24) != CARA_EOK) {
            return fail("pointer init failed (screen scenario)", 9);
        }

        // Massive positive delta clamps at (Width-1, Height-1) = (63, 47).
        post_mouse(1000, 1000);
        if (Leargas_Input_Drain(&p) != 1) {
            return fail("expected 1 event drained for clamp-high", 10);
        }
        if (p.x != 63 || p.y != 47) {
            return fail("pointer not clamped to screen max edge", 11);
        }
        if (screen.pub.MouseX != 63 || screen.pub.MouseY != 47) {
            return fail("Screen.MouseX/Y not updated after Drain", 12);
        }

        // Massive negative delta clamps at (0, 0).
        post_mouse(-2000, -2000);
        if (Leargas_Input_Drain(&p) != 1) {
            return fail("expected 1 event drained for clamp-low", 13);
        }
        if (p.x != 0 || p.y != 0) {
            return fail("pointer not clamped to screen min edge", 14);
        }
    }

    // RAWKEY events are consumed (counted) but don't move the pointer.
    {
        struct DathFramebuffer fb, save;
        struct LeargasPointer p;
        struct LeargasScreen screen;
        fb_init(&fb);
        save_init(&save);
        Leargas_Input_Reset();
        (void)Leargas_Screen_InitInPlace(&screen, &fb, "TestScreen", 0);
        Leargas_Screen_SetActive(&screen);
        (void)Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0xFFFFFFFFu, 0xFF000000u,
                                   10, 10);

        post_key(0x20);
        post_mouse(3, 4);
        post_key(0x21);
        u32 n = Leargas_Input_Drain(&p);
        if (n != 3) {
            return fail("expected 3 events drained (2 key + 1 mouse)", 15);
        }
        if (p.x != 13 || p.y != 14) {
            return fail("RAWKEY events affected pointer position", 16);
        }
    }

    // Multiple mouse events accumulate in order.
    {
        struct DathFramebuffer fb, save;
        struct LeargasPointer p;
        struct LeargasScreen screen;
        fb_init(&fb);
        save_init(&save);
        Leargas_Input_Reset();
        (void)Leargas_Screen_InitInPlace(&screen, &fb, "TestScreen", 0);
        Leargas_Screen_SetActive(&screen);
        (void)Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0xFFFFFFFFu, 0xFF000000u,
                                   32, 24);

        for (i32 i = 1; i <= 8; i++) {
            post_mouse((i16)i, (i16)-i);
        }
        u32 n = Leargas_Input_Drain(&p);
        if (n != 8) {
            return fail("expected 8 mouse events drained", 17);
        }
        // 32 + (1+2+3+4+5+6+7+8) = 32 + 36 = 68 → clamps to 63.
        if (p.x != 63) {
            return fail("accumulated x not clamped at right edge", 18);
        }
        // 24 - 36 = -12 → clamps to 0.
        if (p.y != 0) {
            return fail("accumulated y not clamped at top edge", 19);
        }
    }

    // Cleanup so subsequent test runs don't see stale state.
    Leargas_Input_Reset();
    Leargas_Screen_SetActive(nullptr);

    puts("leargas router ok");
    return 0;
}
