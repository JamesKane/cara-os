// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LD — window primitives.
// Exercises the dual-target portion (InitInPlace / Render /
// FromPub / LinkToScreen / UnlinkFromScreen). OpenWindow /
// CloseWindow live in the kernel image only.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_window: FAIL: %s\n", msg);
    return code;
}

static u32 g_fb_storage[200 * 100];

static void fb_init(struct DathFramebuffer *fb)
{
    fb->base = g_fb_storage;
    fb->width = 200;
    fb->height = 100;
    fb->stride = 200 * 4;
    fb->format = DATH_FMT_RGBA8888;
    fb->bpp = 4;
}

static void clear_fb_to(u32 v)
{
    for (u32 i = 0; i < 200 * 100; i++) {
        g_fb_storage[i] = v;
    }
}

int main(void)
{
    Leargas_Screen_SetActive(nullptr);

    struct DathFramebuffer fb;
    fb_init(&fb);

    struct LeargasScreen screen = { 0 };
    if (Leargas_Screen_InitInPlace(&screen, &fb, "Workbench", 0xFF101020u) != CARA_EOK) {
        return fail("screen init failed", 1);
    }
    Leargas_Screen_SetActive(&screen);

    // Reject NULL / bad geometry / no screen.
    {
        char title_croi[] = "Croi";
        struct LeargasWindow w;
        struct NewWindow nw = {
            .LeftEdge = 10,
            .TopEdge = 10,
            .Width = 80,
            .Height = 40,
            .Title = (UBYTE *)title_croi,
            .Screen = &screen.pub,
            .Flags = WFLG_DRAGBAR | WFLG_CLOSEGADGET,
        };
        if (Leargas_Window_InitInPlace(nullptr, &nw) != CARA_EINVAL) {
            return fail("Init(NULL w) accepted", 2);
        }
        if (Leargas_Window_InitInPlace(&w, nullptr) != CARA_EINVAL) {
            return fail("Init(NULL nw) accepted", 3);
        }
        nw.Width = 0;
        if (Leargas_Window_InitInPlace(&w, &nw) != CARA_EINVAL) {
            return fail("Init accepted zero-width window", 4);
        }
        nw.Width = 80;
        nw.Height = 0;
        if (Leargas_Window_InitInPlace(&w, &nw) != CARA_EINVAL) {
            return fail("Init accepted zero-height window", 5);
        }
        nw.Height = 40;

        // Init falls back to active screen when nw.Screen is null.
        nw.Screen = nullptr;
        if (Leargas_Window_InitInPlace(&w, &nw) != CARA_EOK) {
            return fail("Init failed when nw.Screen=null with active screen", 6);
        }
        if (w.pub.WScreen != &screen.pub) {
            return fail("Init didn't fall back to active screen", 7);
        }

        // Without an active screen and null nw.Screen → EINVAL.
        Leargas_Screen_SetActive(nullptr);
        if (Leargas_Window_InitInPlace(&w, &nw) != CARA_EINVAL) {
            return fail("Init accepted no screen at all", 8);
        }
        Leargas_Screen_SetActive(&screen);
    }

    // Happy-path field seeding.
    char title_croi_main[] = "Croi";
    struct LeargasWindow w = { 0 };
    struct NewWindow nw = {
        .LeftEdge = 20,
        .TopEdge = 30,
        .Width = 100,
        .Height = 50,
        .DetailPen = 0,
        .BlockPen = 1,
        .IDCMPFlags = IDCMP_CLOSEWINDOW | IDCMP_RAWKEY,
        .Flags = WFLG_DRAGBAR | WFLG_CLOSEGADGET | WFLG_ACTIVATE,
        .Title = (UBYTE *)title_croi_main,
        .Screen = &screen.pub,
        .MinWidth = 50,
        .MinHeight = 25,
        .MaxWidth = 200,
        .MaxHeight = 100,
        .Type = WBENCHSCREEN,
    };
    if (Leargas_Window_InitInPlace(&w, &nw) != CARA_EOK) {
        return fail("InitInPlace happy path failed", 9);
    }
    if (w.pub.LeftEdge != 20 || w.pub.TopEdge != 30 || w.pub.Width != 100 ||
        w.pub.Height != 50) {
        return fail("geometry not stored", 10);
    }
    if (w.pub.WScreen != &screen.pub) {
        return fail("WScreen not stored", 11);
    }
    if (!w.pub.Title || strcmp((const char *)w.pub.Title, "Croi") != 0) {
        return fail("Title not copied / not pointed at brand buffer", 12);
    }
    if ((const char *)w.pub.Title != w.title_buf) {
        return fail("Title pointer doesn't reference brand-owned buffer", 13);
    }
    if (w.pub.IDCMPFlags != (IDCMP_CLOSEWINDOW | IDCMP_RAWKEY)) {
        return fail("IDCMPFlags not stored", 14);
    }
    if (!(w.pub.Flags & WFLG_DRAGBAR) || !(w.pub.Flags & WFLG_CLOSEGADGET)) {
        return fail("Flags not stored", 15);
    }
    if (w.pub.MinWidth != 50 || w.pub.MaxWidth != 200) {
        return fail("Min/Max not stored", 16);
    }
    if (w.pub.BorderTop != LEARGAS_WINDOW_DEFAULT_BORDER_TOP) {
        return fail("BorderTop not seeded for DRAGBAR window", 17);
    }
    if (w.pub.RPort || w.pub.BorderRPort || w.pub.WLayer) {
        return fail("V36+-deviation pointers not nullptr", 18);
    }

    // Borderless windows zero out all border insets.
    {
        struct LeargasWindow bw;
        struct NewWindow bnw = nw;
        bnw.Flags = WFLG_BORDERLESS;
        if (Leargas_Window_InitInPlace(&bw, &bnw) != CARA_EOK) {
            return fail("borderless init failed", 19);
        }
        if (bw.pub.BorderTop != 0 || bw.pub.BorderBottom != 0 || bw.pub.BorderLeft != 0 ||
            bw.pub.BorderRight != 0) {
            return fail("borderless didn't zero border insets", 20);
        }
    }

    // FromPub round-trip + null safety.
    if (Leargas_Window_FromPub(&w.pub) != &w) {
        return fail("FromPub round-trip mismatch", 21);
    }
    if (Leargas_Window_FromPub(nullptr) != nullptr) {
        return fail("FromPub(NULL) didn't return null", 22);
    }

    // LinkToScreen / UnlinkFromScreen.
    if (screen.pub.FirstWindow != nullptr) {
        return fail("FirstWindow non-null pre-link", 23);
    }
    Leargas_Window_LinkToScreen(&w);
    if (screen.pub.FirstWindow != &w.pub) {
        return fail("LinkToScreen didn't set FirstWindow", 24);
    }
    if (w.pub.NextWindow != nullptr) {
        return fail("First-linked window's NextWindow not null", 25);
    }
    // Link a second window — should chain in front.
    char title_other[] = "Other";
    struct LeargasWindow w2 = { 0 };
    struct NewWindow nw2 = nw;
    nw2.LeftEdge = 130;
    nw2.Title = (UBYTE *)title_other;
    if (Leargas_Window_InitInPlace(&w2, &nw2) != CARA_EOK) {
        return fail("second window init failed", 26);
    }
    Leargas_Window_LinkToScreen(&w2);
    if (screen.pub.FirstWindow != &w2.pub) {
        return fail("second link didn't head-insert", 27);
    }
    if (w2.pub.NextWindow != &w.pub) {
        return fail("second window's NextWindow != first window", 28);
    }
    // Unlink the second (head).
    Leargas_Window_UnlinkFromScreen(&w2);
    if (screen.pub.FirstWindow != &w.pub) {
        return fail("Unlink head left wrong FirstWindow", 29);
    }
    // Unlink the only remaining.
    Leargas_Window_UnlinkFromScreen(&w);
    if (screen.pub.FirstWindow != nullptr) {
        return fail("Unlink last window didn't null FirstWindow", 30);
    }
    // Unlink an already-unlinked window is a no-op.
    Leargas_Window_UnlinkFromScreen(&w);
    if (screen.pub.FirstWindow != nullptr) {
        return fail("double-unlink corrupted state", 31);
    }

    // Render: paint to a known background, render the window, verify
    // pixels inside the window rect are non-background and the
    // outside is unchanged.
    clear_fb_to(0xDEADBEEFu);
    Leargas_Window_Render(&w);

    // Pixel just outside the top-left corner — unchanged.
    if (g_fb_storage[(20 - 1) + (30 - 1) * 200] != 0xDEADBEEFu) {
        return fail("Render leaked outside top-left corner", 32);
    }
    // Pixel just inside the top-left corner — must NOT match background.
    if (g_fb_storage[20 + 30 * 200] == 0xDEADBEEFu) {
        return fail("Render didn't paint top-left corner", 33);
    }
    // Pixel inside the body (well below the title bar) — must be body color.
    {
        u32 px = g_fb_storage[40 + (30 + 30) * 200];
        if (px == 0xDEADBEEFu) {
            return fail("Render didn't paint window body", 34);
        }
    }
    // Pixel just below the bottom edge — unchanged.
    if (g_fb_storage[40 + (30 + 50) * 200] != 0xDEADBEEFu) {
        return fail("Render leaked below bottom edge", 35);
    }
    // Pixel just to the right of the right edge — unchanged.
    if (g_fb_storage[(20 + 100) + 40 * 200] != 0xDEADBEEFu) {
        return fail("Render leaked right of right edge", 36);
    }

    // Render with no active screen / null screen on the window —
    // safe no-op that doesn't paint or crash.
    {
        struct LeargasWindow orphan = { 0 };
        struct NewWindow onw = nw;
        if (Leargas_Window_InitInPlace(&orphan, &onw) != CARA_EOK) {
            return fail("orphan init failed", 37);
        }
        // Pretend the screen pointer was set without a brand wrapper.
        // (FromPub returns null on a non-LeargasScreen pub if its
        // offset doesn't line up — we test via direct null instead.)
        clear_fb_to(0xCAFEBABEu);
        orphan.pub.WScreen = nullptr;
        Leargas_Window_Render(&orphan);
        if (g_fb_storage[0] != 0xCAFEBABEu) {
            return fail("Render(null WScreen) painted anyway", 38);
        }
    }

    // Cleanup so subsequent test runs start clean.
    Leargas_Screen_SetActive(nullptr);

    puts("leargas window ok");
    return 0;
}
