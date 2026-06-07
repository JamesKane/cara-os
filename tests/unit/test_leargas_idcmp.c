// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LF — the dual-target IntuiMessage
// translation (Leargas_BuildIntuiMessage). The MsgPort delivery path
// (RouteKey / GetMsg / UserPort) is kernel-only and covered by the
// in-kernel KERNEL_TEST(idcmp_rawkey).

#include <cara/dath.h>
#include <cara/leargas.h>
#include <devices/inputevent.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_idcmp: FAIL: %s\n", msg);
    return code;
}

static u32 g_fb_storage[200 * 120];

static void fb_init(struct DathFramebuffer *fb)
{
    fb->base = g_fb_storage;
    fb->width = 200;
    fb->height = 120;
    fb->stride = 200 * 4;
    fb->format = DATH_FMT_RGBA8888;
    fb->bpp = 4;
}

int main(void)
{
    Leargas_Screen_SetActive(nullptr);

    struct DathFramebuffer fb;
    fb_init(&fb);
    struct LeargasScreen screen = { 0 };
    if (Leargas_Screen_InitInPlace(&screen, &fb, "S", 0xFF101020u) != CARA_EOK) {
        return fail("screen init failed", 1);
    }
    Leargas_Screen_SetActive(&screen);
    // Pointer at screen (50, 40) — set as the router would mirror it.
    screen.pub.MouseX = 50;
    screen.pub.MouseY = 40;

    char title[] = "W";
    struct LeargasWindow w = { 0 };
    struct NewWindow nw = {
        .LeftEdge = 10,
        .TopEdge = 20,
        .Width = 100,
        .Height = 60,
        .Title = (UBYTE *)title,
        .Screen = &screen.pub,
        .Flags = WFLG_DRAGBAR,
        .IDCMPFlags = IDCMP_RAWKEY,
    };
    if (Leargas_Window_InitInPlace(&w, &nw) != CARA_EOK) {
        return fail("window init failed", 2);
    }

    // ---- RAWKEY translation ------------------------------------------------
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWKEY,
        .ie_code = 0x20, // rawkey for 'A'
        .ie_qualifier = IEQUALIFIER_LSHIFT | IEQUALIFIER_LALT,
        .ie_ts_ns = 3ull * 1000000000ull + 456000ull, // 3s + 456us
    };

    struct IntuiMessage im;
    memset(&im, 0xAB, sizeof(im)); // poison so we know Build set every field
    Leargas_BuildIntuiMessage(&im, &w.pub, &ev);

    if (im.Class != IDCMP_RAWKEY) {
        return fail("Class not IDCMP_RAWKEY", 3);
    }
    if (im.Code != 0x20) {
        return fail("Code not copied", 4);
    }
    if (im.Qualifier != (IEQUALIFIER_LSHIFT | IEQUALIFIER_LALT)) {
        return fail("Qualifier not copied", 5);
    }
    if (im.IAddress != nullptr) {
        return fail("IAddress not null for RAWKEY", 6);
    }
    if (im.IDCMPWindow != &w.pub) {
        return fail("IDCMPWindow back-pointer wrong", 7);
    }
    // Window-relative pointer: screen (50,40) minus window origin (10,20).
    if (im.MouseX != 40 || im.MouseY != 20) {
        return fail("MouseX/MouseY not window-relative", 8);
    }
    if (im.Seconds != 3 || im.Micros != 456) {
        return fail("timestamp split wrong", 9);
    }
    if (im.ExecMessage.mn_Length != (UWORD)sizeof(struct IntuiMessage)) {
        return fail("mn_Length not set", 10);
    }
    if (im.SpecialLink != nullptr) {
        return fail("SpecialLink not null", 11);
    }

    // ---- Non-RAWKEY class maps to Class 0 (no Phase 1 consumer) -------------
    struct LeargasInputEvent mev = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = IECODE_LBUTTON,
    };
    struct IntuiMessage im2 = { 0 };
    Leargas_BuildIntuiMessage(&im2, &w.pub, &mev);
    if (im2.Class != 0) {
        return fail("non-RAWKEY class didn't map to 0", 12);
    }

    // ---- No screen → window-relative coords fall back to origin ------------
    {
        struct LeargasWindow ws = w;
        ws.pub.WScreen = nullptr;
        struct IntuiMessage im3;
        memset(&im3, 0x55, sizeof(im3));
        Leargas_BuildIntuiMessage(&im3, &ws.pub, &ev);
        if (im3.MouseX != 0 || im3.MouseY != 0) {
            return fail("no-screen MouseX/Y not zeroed", 13);
        }
    }

    // ---- Null-safety: no writes, no crash ----------------------------------
    Leargas_BuildIntuiMessage(nullptr, &w.pub, &ev);
    Leargas_BuildIntuiMessage(&im, nullptr, &ev);
    Leargas_BuildIntuiMessage(&im, &w.pub, nullptr);

    Leargas_Screen_SetActive(nullptr);
    puts("leargas idcmp ok");
    return 0;
}
