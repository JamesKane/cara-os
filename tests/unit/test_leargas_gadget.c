// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LG — the gadget framework.
// Exercises chain management (AddGadget / RemoveGadget), hit-testing,
// rendering (button face / border / selected shade), the active-gadget
// pointer, and the router's left-button press/release select path.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <devices/inputevent.h>
#include <stdio.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_gadget: FAIL: %s\n", msg);
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

static struct Gadget make_gadget(i16 x, i16 y, i16 w, i16 h)
{
    return (struct Gadget){
        .LeftEdge = x,
        .TopEdge = y,
        .Width = w,
        .Height = h,
        .GadgetType = GTYP_BOOLGADGET,
        .Activation = GACT_RELVERIFY,
    };
}

static void post_button(u16 code)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = code,
        .ie_qualifier = IEQUALIFIER_RELATIVEMOUSE,
    };
    (void)Leargas_Input_Post(&ev);
}

int main(void)
{
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);

    struct DathFramebuffer fb;
    fb_init(&fb);
    struct LeargasScreen screen = { 0 };
    if (Leargas_Screen_InitInPlace(&screen, &fb, "S", 0xFF101020u) != CARA_EOK) {
        return fail("screen init failed", 1);
    }
    Leargas_Screen_SetActive(&screen);

    char wtitle[] = "W";
    struct LeargasWindow w = { 0 };
    struct NewWindow nw = {
        .LeftEdge = 10,
        .TopEdge = 10,
        .Width = 150,
        .Height = 90,
        .Title = (UBYTE *)wtitle,
        .Screen = &screen.pub,
        .Flags = WFLG_DRAGBAR,
    };
    if (Leargas_Window_InitInPlace(&w, &nw) != CARA_EOK) {
        return fail("window init failed", 2);
    }
    Leargas_Window_LinkToScreen(&w);

    // ---- Chain management --------------------------------------------------
    struct Gadget g1 = make_gadget(20, 30, 60, 20); // window-rel
    struct Gadget g2 = make_gadget(20, 55, 60, 20);
    struct Gadget g3 = make_gadget(90, 30, 40, 20);

    if (w.pub.FirstGadget != nullptr) {
        return fail("FirstGadget not null initially", 3);
    }
    Leargas_AddGadget(&w.pub, &g1);
    Leargas_AddGadget(&w.pub, &g2);
    Leargas_AddGadget(&w.pub, &g3);
    if (w.pub.FirstGadget != &g1 || g1.NextGadget != &g2 || g2.NextGadget != &g3 ||
        g3.NextGadget != nullptr) {
        return fail("AddGadget didn't append in order", 4);
    }
    // Re-adding a linked gadget is a no-op (no chain corruption).
    Leargas_AddGadget(&w.pub, &g2);
    if (g3.NextGadget != nullptr || g2.NextGadget != &g3) {
        return fail("re-AddGadget corrupted the chain", 5);
    }
    // Remove the middle.
    Leargas_RemoveGadget(&w.pub, &g2);
    if (w.pub.FirstGadget != &g1 || g1.NextGadget != &g3 || g2.NextGadget != nullptr) {
        return fail("RemoveGadget(middle) wrong", 6);
    }
    // Remove an unlinked gadget — no-op.
    Leargas_RemoveGadget(&w.pub, &g2);
    if (w.pub.FirstGadget != &g1 || g1.NextGadget != &g3) {
        return fail("RemoveGadget(unlinked) corrupted chain", 7);
    }
    // Put g2 back for the rest of the test.
    Leargas_AddGadget(&w.pub, &g2); // chain: g1 -> g3 -> g2

    // ---- Hit-testing (window-relative) -------------------------------------
    if (Leargas_Gadget_HitTest(nullptr, 0, 0) != nullptr) {
        return fail("HitTest(null window) not null", 8);
    }
    if (Leargas_Gadget_HitTest(&w.pub, 0, 0) != nullptr) {
        return fail("HitTest over no gadget not null", 9);
    }
    if (Leargas_Gadget_HitTest(&w.pub, 25, 35) != &g1) {
        return fail("HitTest inside g1 didn't return g1", 10);
    }
    if (Leargas_Gadget_HitTest(&w.pub, 100, 35) != &g3) {
        return fail("HitTest inside g3 didn't return g3", 11);
    }
    // Right/bottom edges exclusive: g1 spans x[20,80), so x=80 misses.
    if (Leargas_Gadget_HitTest(&w.pub, 80, 35) != nullptr) {
        return fail("HitTest treated right edge as inclusive", 12);
    }
    // Disabled gadgets are skipped.
    g1.Flags |= GFLG_DISABLED;
    if (Leargas_Gadget_HitTest(&w.pub, 25, 35) != nullptr) {
        return fail("HitTest didn't skip GFLG_DISABLED", 13);
    }
    g1.Flags &= ~(UWORD)GFLG_DISABLED;

    // ---- Active gadget -----------------------------------------------------
    Leargas_Gadget_Reset();
    if (Leargas_ActiveGadget() != nullptr) {
        return fail("ActiveGadget not null after reset", 14);
    }
    Leargas_SetActiveGadget(&g1);
    if (Leargas_ActiveGadget() != &g1) {
        return fail("SetActiveGadget didn't stick", 15);
    }
    Leargas_Gadget_Reset();

    // ---- Render: face / border / selected shade ----------------------------
    // g3 has no GadgetText, so its centre is a clean face pixel. Screen
    // coords: window (10,10) + g3 (90,30) = (100,40), size 40x20.
    const u32 face_rest = (u32)Dath_RGB(0xB0, 0xB0, 0xB0);
    const u32 face_sel = (u32)Dath_RGB(0x70, 0x70, 0x70);
    const u32 border = (u32)Dath_RGB(0x30, 0x30, 0x30);
    const u32 centre = (100 + 20) + (40 + 10) * 200; // (120, 50)
    const u32 corner = 100 + 40 * 200;               // (100, 40) top-left

    g3.Flags &= ~(UWORD)GFLG_SELECTED;
    Leargas_Gadget_Render(&w.pub, &g3);
    if (g_fb_storage[centre] != face_rest) {
        return fail("unselected gadget face wrong", 16);
    }
    if (g_fb_storage[corner] != border) {
        return fail("gadget border wrong", 17);
    }
    g3.Flags |= GFLG_SELECTED;
    Leargas_Gadget_Render(&w.pub, &g3);
    if (g_fb_storage[centre] != face_sel) {
        return fail("selected gadget face not pressed shade", 18);
    }
    g3.Flags &= ~(UWORD)GFLG_SELECTED;

    // ---- Router press/release select ---------------------------------------
    Leargas_Input_Reset();
    Leargas_Gadget_Reset();
    Leargas_SetActiveWindow(&w.pub);

    struct DathFramebuffer save;
    save_init(&save);
    struct LeargasPointer p;
    // Pointer at screen (45, 45): inside the window and inside g1
    // (screen x[30,90) y[40,60)).
    if (Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0xFFFFFFFFu, 0xFF000000u, 45,
                             45) != CARA_EOK) {
        return fail("pointer init failed", 19);
    }

    post_button(IECODE_LBUTTON);
    if (Leargas_Input_Drain(&p) != 1) {
        return fail("expected 1 event for button-down", 20);
    }
    if (!(g1.Flags & GFLG_SELECTED)) {
        return fail("button-down didn't select g1", 21);
    }
    if (Leargas_ActiveGadget() != &g1) {
        return fail("button-down didn't set active gadget", 22);
    }

    post_button(IECODE_LBUTTON | IECODE_UP_PREFIX);
    if (Leargas_Input_Drain(&p) != 1) {
        return fail("expected 1 event for button-up", 23);
    }
    if (g1.Flags & GFLG_SELECTED) {
        return fail("button-up didn't clear GFLG_SELECTED", 24);
    }

    // A press over the window but over no gadget selects nothing.
    Leargas_Gadget_Reset();
    Leargas_Pointer_Move(&p, 15, 15); // window (10,10)-relative (5,5): no gadget there
    post_button(IECODE_LBUTTON);
    (void)Leargas_Input_Drain(&p);
    if (Leargas_ActiveGadget() != nullptr) {
        return fail("press over empty window area selected a gadget", 25);
    }
    post_button(IECODE_LBUTTON | IECODE_UP_PREFIX);
    (void)Leargas_Input_Drain(&p);

    Leargas_Input_Reset();
    Leargas_Screen_SetActive(nullptr);
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    puts("leargas gadget ok");
    return 0;
}
