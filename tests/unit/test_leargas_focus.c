// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LE — focus + activation.
// Exercises the dual-target focus module (Leargas_ActiveWindow /
// SetActiveWindow / Window_HitTest), the active/inactive title-bar
// chrome distinction in Leargas_Window_Render, and the router's
// left-button-down → focus path driven through the L0 input ring.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <devices/inputevent.h>
#include <stdio.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_focus: FAIL: %s\n", msg);
    return code;
}

static u32 g_fb_storage[200 * 120];
static u32 g_save_storage[16 * 16];

static void fb_init(struct DathFramebuffer *fb)
{
    fb->base = g_fb_storage;
    fb->width = 200;
    fb->height = 120;
    fb->stride = 200 * 4;
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

// Seed a window in-place and link it to the screen. Geometry only;
// DRAGBAR so a title bar renders, CLOSEGADGET to mirror Clar's chrome.
static void make_window(struct LeargasWindow *w, struct Screen *screen, char *title, i16 x, i16 y,
                        i16 width, i16 height)
{
    struct NewWindow nw = {
        .LeftEdge = x,
        .TopEdge = y,
        .Width = width,
        .Height = height,
        .Title = (UBYTE *)title,
        .Screen = screen,
        .Flags = WFLG_DRAGBAR | WFLG_CLOSEGADGET,
    };
    (void)Leargas_Window_InitInPlace(w, &nw);
    Leargas_Window_LinkToScreen(w);
}

static void post_mouse(i16 dx, i16 dy)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = IECODE_NOBUTTON,
        .ie_qualifier = IEQUALIFIER_RELATIVEMOUSE,
        .ie_dx = dx,
        .ie_dy = dy,
    };
    (void)Leargas_Input_Post(&ev);
}

static void post_lbutton_down(void)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = IECODE_LBUTTON,
        .ie_qualifier = IEQUALIFIER_RELATIVEMOUSE | IEQUALIFIER_LEFTBUTTON,
    };
    (void)Leargas_Input_Post(&ev);
}

int main(void)
{
    Leargas_Input_Reset();
    Leargas_Screen_SetActive(nullptr);
    Leargas_Focus_Reset();

    struct DathFramebuffer fb;
    fb_init(&fb);
    struct LeargasScreen screen = { 0 };
    if (Leargas_Screen_InitInPlace(&screen, &fb, "Workbench", 0xFF101020u) != CARA_EOK) {
        return fail("screen init failed", 1);
    }
    Leargas_Screen_SetActive(&screen);

    // Two windows. A links first, B second, so the screen list is
    // B → A (FirstWindow is the front-most). They overlap in
    // x[60,90) y[30,50).
    char title_a[] = "Alpha";
    char title_b[] = "Bravo";
    struct LeargasWindow wa = { 0 };
    struct LeargasWindow wb = { 0 };
    make_window(&wa, &screen.pub, title_a, 10, 10, 80, 40); // x[10,90) y[10,50)
    make_window(&wb, &screen.pub, title_b, 60, 30, 80, 40); // x[60,140) y[30,70)

    // ---- Hit-testing -------------------------------------------------------
    if (Leargas_Window_HitTest(nullptr, 0, 0) != nullptr) {
        return fail("HitTest(null screen) not null", 2);
    }
    if (Leargas_Window_HitTest(&screen.pub, 5, 5) != nullptr) {
        return fail("HitTest over empty screen area not null", 3);
    }
    if (Leargas_Window_HitTest(&screen.pub, 15, 15) != &wa.pub) {
        return fail("HitTest in A-only region didn't return A", 4);
    }
    if (Leargas_Window_HitTest(&screen.pub, 120, 60) != &wb.pub) {
        return fail("HitTest in B-only region didn't return B", 5);
    }
    // Overlap resolves to the front-most window (B).
    if (Leargas_Window_HitTest(&screen.pub, 70, 40) != &wb.pub) {
        return fail("HitTest in overlap didn't return front-most (B)", 6);
    }
    // Right/bottom edges are exclusive: A spans x[10,90), so x=90 misses.
    if (Leargas_Window_HitTest(&screen.pub, 90, 15) != nullptr) {
        return fail("HitTest treated right edge as inclusive", 7);
    }
    if (Leargas_Window_HitTest(&screen.pub, 150, 100) != nullptr) {
        return fail("HitTest off all windows not null", 8);
    }

    // ---- SetActiveWindow flag + state transitions --------------------------
    Leargas_Focus_Reset();
    if (Leargas_ActiveWindow() != nullptr) {
        return fail("ActiveWindow not null after reset", 9);
    }

    Leargas_SetActiveWindow(&wa.pub);
    if (Leargas_ActiveWindow() != &wa.pub) {
        return fail("ActiveWindow != A after activating A", 10);
    }
    if (!(wa.pub.Flags & WFLG_WINDOWACTIVE)) {
        return fail("A missing WFLG_WINDOWACTIVE after activation", 11);
    }
    if (wb.pub.Flags & WFLG_WINDOWACTIVE) {
        return fail("B has WFLG_WINDOWACTIVE while A is active", 12);
    }

    // Switching focus deactivates the previous window.
    Leargas_SetActiveWindow(&wb.pub);
    if (Leargas_ActiveWindow() != &wb.pub) {
        return fail("ActiveWindow != B after switch", 13);
    }
    if (wa.pub.Flags & WFLG_WINDOWACTIVE) {
        return fail("A still active after switching to B", 14);
    }
    if (!(wb.pub.Flags & WFLG_WINDOWACTIVE)) {
        return fail("B not active after switch", 15);
    }

    // Re-activating the already-active window is a no-op (flag stays).
    Leargas_SetActiveWindow(&wb.pub);
    if (Leargas_ActiveWindow() != &wb.pub || !(wb.pub.Flags & WFLG_WINDOWACTIVE)) {
        return fail("idempotent re-activation disturbed state", 16);
    }

    // Clearing focus deactivates the outgoing window.
    Leargas_SetActiveWindow(nullptr);
    if (Leargas_ActiveWindow() != nullptr) {
        return fail("ActiveWindow not null after clear", 17);
    }
    if (wb.pub.Flags & WFLG_WINDOWACTIVE) {
        return fail("B still active after clearing focus", 18);
    }

    // ---- Active vs. inactive title-bar chrome (LE.3) -----------------------
    // Sample a title-bar fill pixel left of the title text: A's title
    // bar fills x[11,89) y[11,21); text starts at x=14, so (12,15) is
    // pure chrome background in both states.
    const u32 idx = 12 + 15 * 200;
    const u32 inactive_rgb = (u32)Dath_RGB(0x20, 0x40, 0x80);
    const u32 active_rgb = (u32)Dath_RGB(0x38, 0x78, 0xD0);

    Leargas_Focus_Reset();
    wa.pub.Flags &= ~(ULONG)WFLG_WINDOWACTIVE;
    Leargas_Window_Render(&wa);
    if (g_fb_storage[idx] != inactive_rgb) {
        return fail("inactive window title bar not inactive chrome", 19);
    }

    Leargas_SetActiveWindow(&wa.pub);
    if (g_fb_storage[idx] != active_rgb) {
        return fail("active window title bar not active chrome", 20);
    }
    if (g_fb_storage[idx] == inactive_rgb) {
        return fail("active chrome identical to inactive chrome", 21);
    }

    // ---- Router: left-button-down drives focus -----------------------------
    Leargas_Input_Reset();
    Leargas_Focus_Reset();

    struct DathFramebuffer save;
    save_init(&save);
    struct LeargasPointer p;
    if (Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0xFFFFFFFFu, 0xFF000000u, 15,
                             15) != CARA_EOK) {
        return fail("pointer init failed", 22);
    }

    // Click at the pointer's current position (15,15) → inside A.
    post_lbutton_down();
    if (Leargas_Input_Drain(&p) != 1) {
        return fail("expected 1 event drained for click-on-A", 23);
    }
    if (Leargas_ActiveWindow() != &wa.pub) {
        return fail("click on A didn't focus A", 24);
    }

    // Move the pointer into B-only territory (120,60) then click.
    post_mouse(105, 45);
    post_lbutton_down();
    if (Leargas_Input_Drain(&p) != 2) {
        return fail("expected 2 events drained for move+click-on-B", 25);
    }
    if (p.x != 120 || p.y != 60) {
        return fail("pointer not at expected position before B click", 26);
    }
    if (Leargas_ActiveWindow() != &wb.pub) {
        return fail("click on B didn't move focus to B", 27);
    }
    if (wa.pub.Flags & WFLG_WINDOWACTIVE) {
        return fail("A still active after focusing B via click", 28);
    }

    // Move the pointer over empty screen and click — focus unchanged.
    post_mouse(-115, -55); // → (5,5), over no window
    post_lbutton_down();
    if (Leargas_Input_Drain(&p) != 2) {
        return fail("expected 2 events drained for move+click-empty", 29);
    }
    if (Leargas_ActiveWindow() != &wb.pub) {
        return fail("click on empty area changed focus", 30);
    }

    // Cleanup so subsequent test runs start clean.
    Leargas_Input_Reset();
    Leargas_Screen_SetActive(nullptr);
    Leargas_Focus_Reset();

    puts("leargas focus ok");
    return 0;
}
