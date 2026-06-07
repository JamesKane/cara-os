// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LB — screen substrate. Exercises
// the dual-target portion (InitInPlace / SetActive / ActiveScreen /
// FromPub); OpenScreen / CloseScreen run in the kernel image only.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_screen: FAIL: %s\n", msg);
    return code;
}

static u32 g_fb_storage[64 * 48];

static void fb_init(struct DathFramebuffer *fb)
{
    fb->base = g_fb_storage;
    fb->width = 64;
    fb->height = 48;
    fb->stride = 64 * 4;
    fb->format = DATH_FMT_RGBA8888;
    fb->bpp = 4;
}

int main(void)
{
    // Reject NULLs.
    {
        struct LeargasScreen s;
        struct DathFramebuffer fb;
        fb_init(&fb);
        if (Leargas_Screen_InitInPlace(nullptr, &fb, "x", 0) != CARA_EINVAL) {
            return fail("InitInPlace(NULL s) accepted", 1);
        }
        if (Leargas_Screen_InitInPlace(&s, nullptr, "x", 0) != CARA_EINVAL) {
            return fail("InitInPlace(NULL fb) accepted", 2);
        }
    }

    // Reject zero-sized framebuffer.
    {
        struct LeargasScreen s;
        struct DathFramebuffer fb;
        fb_init(&fb);
        fb.width = 0;
        if (Leargas_Screen_InitInPlace(&s, &fb, "x", 0) != CARA_EINVAL) {
            return fail("InitInPlace accepted zero-width fb", 3);
        }
    }

    // Happy path. Public Screen field set populated, brand-private
    // fb pointer stored, default chrome metrics seeded.
    struct LeargasScreen s = { 0 };
    struct DathFramebuffer fb;
    fb_init(&fb);
    if (Leargas_Screen_InitInPlace(&s, &fb, "Workbench", 0xFF101020u) != CARA_EOK) {
        return fail("InitInPlace returned non-EOK on happy path", 4);
    }
    if (s.fb != &fb) {
        return fail("brand fb pointer not stored", 5);
    }
    if (s.pub.Width != 64 || s.pub.Height != 48) {
        return fail("dimensions not derived from fb", 6);
    }
    if (s.pub.LeftEdge != 0 || s.pub.TopEdge != 0) {
        return fail("origin not zero", 7);
    }
    if (!(s.pub.Flags & WBENCHSCREEN) || !(s.pub.Flags & SHOWTITLE)) {
        return fail("Flags missing WBENCHSCREEN | SHOWTITLE", 8);
    }
    if (s.pub.BarHeight != LEARGAS_DEFAULT_BAR_HEIGHT) {
        return fail("BarHeight not seeded", 9);
    }
    if (s.pub.WBorTop != LEARGAS_DEFAULT_WBOR_TOP || s.pub.WBorLeft != LEARGAS_DEFAULT_WBOR_LEFT ||
        s.pub.WBorRight != LEARGAS_DEFAULT_WBOR_RIGHT ||
        s.pub.WBorBottom != LEARGAS_DEFAULT_WBOR_BOTTOM) {
        return fail("WBor* metrics not seeded", 10);
    }
    if (!s.pub.Title || strcmp((const char *)s.pub.Title, "Workbench") != 0) {
        return fail("Title not copied / pointed at brand buffer", 11);
    }
    if ((const char *)s.pub.Title != s.title_buf) {
        return fail("Title doesn't point at brand-owned buffer", 12);
    }
    if (!s.pub.DefaultTitle || strcmp((const char *)s.pub.DefaultTitle, "Workbench") != 0) {
        return fail("DefaultTitle not initialised", 13);
    }

    // Pointer-where-V36+-embeds fields are nullptr in Phase 1.
    if (s.pub.RastPort || s.pub.ViewPort || s.pub.BitMap || s.pub.LayerInfo) {
        return fail("Phase 1 deviation fields not nullptr", 14);
    }

    // NULL title is tolerated (init produces an empty string).
    {
        struct LeargasScreen s2 = { 0 };
        if (Leargas_Screen_InitInPlace(&s2, &fb, nullptr, 0) != CARA_EOK) {
            return fail("InitInPlace rejected NULL title", 15);
        }
        if (s2.pub.Title == nullptr || s2.pub.Title[0] != '\0') {
            return fail("NULL title did not produce empty string", 16);
        }
    }

    // Title clipped at LEARGAS_SCREEN_TITLE_MAX-1 with NUL terminator.
    {
        char long_title[LEARGAS_SCREEN_TITLE_MAX + 16];
        for (u32 i = 0; i < sizeof(long_title) - 1; i++) {
            long_title[i] = 'A';
        }
        long_title[sizeof(long_title) - 1] = '\0';
        struct LeargasScreen s3 = { 0 };
        if (Leargas_Screen_InitInPlace(&s3, &fb, long_title, 0) != CARA_EOK) {
            return fail("InitInPlace rejected long title", 17);
        }
        if (strlen(s3.title_buf) != LEARGAS_SCREEN_TITLE_MAX - 1) {
            return fail("long title not clipped to MAX-1", 18);
        }
        if (s3.title_buf[LEARGAS_SCREEN_TITLE_MAX - 1] != '\0') {
            return fail("long title not NUL-terminated", 19);
        }
    }

    // Active-screen tracking: starts nullptr, settable, clearable.
    Leargas_Screen_SetActive(nullptr);
    if (Leargas_ActiveScreen() != nullptr) {
        return fail("ActiveScreen not nullptr after explicit clear", 20);
    }
    Leargas_Screen_SetActive(&s);
    if (Leargas_ActiveScreen() != &s.pub) {
        return fail("ActiveScreen != public pointer of set screen", 21);
    }
    Leargas_Screen_SetActive(nullptr);
    if (Leargas_ActiveScreen() != nullptr) {
        return fail("ActiveScreen not nullptr after re-clear", 22);
    }

    // FromPub recovers the brand wrapper and is NULL-safe.
    if (Leargas_Screen_FromPub(&s.pub) != &s) {
        return fail("FromPub round-trip mismatch", 23);
    }
    if (Leargas_Screen_FromPub(nullptr) != nullptr) {
        return fail("FromPub(NULL) did not return NULL", 24);
    }

    // Layout invariant: the public Screen lives at offset 0 within
    // LeargasScreen so a plain cast is safe (CroiMsgPort pattern).
    if ((char *)&s != (char *)&s.pub) {
        return fail("pub field is not at offset 0 in LeargasScreen", 25);
    }

    puts("leargas screen ok");
    return 0;
}
