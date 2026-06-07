// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(string_gadgetup): the LH string-Inntin path end-to-end
// through the real per-window IDCMP port. Opens a window with a string
// gadget, activates it, types via the L0 ring + router, and asserts the
// buffer fills and that Return delivers an IDCMP_GADGETUP IntuiMessage
// (Code = GadgetID, IAddress = gadget) to the window's UserPort.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/test.h>
#include <cara/types.h>
#include <devices/inputevent.h>

static UBYTE g_kbuf[32];

KERNEL_TEST(string_gadgetup)
{
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);
    Leargas_SetKeyRouter(Leargas_IDCMP_RouteKey);
    Leargas_SetGadgetRouter(Leargas_IDCMP_PostGadgetUp);

    struct DathFramebuffer fb;
    TEST_ASSERT(ctx, Dath_AllocBitmap(&fb, 200, 120, DATH_FMT_RGBA8888) == CARA_EOK, "fb alloc");
    struct Screen *scr = Leargas_OpenScreen(&fb, "T", Dath_RGB(0x10, 0x10, 0x20));
    TEST_ASSERT(ctx, scr != nullptr, "open screen");

    char wtitle[] = "Win";
    struct NewWindow nw = {
        .LeftEdge = 10,
        .TopEdge = 10,
        .Width = 150,
        .Height = 80,
        .Flags = WFLG_DRAGBAR | WFLG_ACTIVATE,
        .IDCMPFlags = IDCMP_GADGETUP | IDCMP_RAWKEY,
        .Title = (UBYTE *)wtitle,
        .Screen = scr,
    };
    struct Window *win = Leargas_OpenWindow(&nw);
    TEST_ASSERT(ctx, win != nullptr, "open window");
    TEST_ASSERT(ctx, win->UserPort != nullptr, "window got a UserPort");

    struct StringInfo si = { 0 };
    si.Buffer = g_kbuf;
    si.MaxChars = (WORD)sizeof(g_kbuf);
    g_kbuf[0] = 0;

    struct Gadget g = {
        .LeftEdge = 20,
        .TopEdge = 40,
        .Width = 100,
        .Height = 14,
        .GadgetType = GTYP_STRGADGET,
        .GadgetID = 7,
        .SpecialInfo = &si,
    };
    Leargas_AddGadget(win, &g);
    Leargas_SetActiveGadget(&g); // simulate a click into the Inntin

    struct DathFramebuffer save;
    TEST_ASSERT(ctx,
                Dath_AllocBitmap(&save, leargas_pointer_arrow.width, leargas_pointer_arrow.height,
                                 fb.format) == CARA_EOK,
                "pointer save alloc");
    struct LeargasPointer p;
    TEST_ASSERT(ctx,
                Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow,
                                     Dath_RGB(0xFF, 0xFF, 0xFF), Dath_RGB(0, 0, 0), 100,
                                     70) == CARA_EOK,
                "pointer init");

    // Type "Hi" (shift+h, i), then Return.
    struct LeargasInputEvent ev = { .ie_class = IECLASS_RAWKEY };
    ev.ie_code = 0x25; // 'h'
    ev.ie_qualifier = IEQUALIFIER_LSHIFT;
    TEST_ASSERT(ctx, Leargas_Input_Post(&ev), "post H");
    ev.ie_code = 0x17; // 'i'
    ev.ie_qualifier = 0;
    TEST_ASSERT(ctx, Leargas_Input_Post(&ev), "post i");
    (void)Leargas_Input_Drain(&p);

    TEST_ASSERT(ctx, g_kbuf[0] == 'H' && g_kbuf[1] == 'i' && si.NumChars == 2,
                "typing filled the buffer");
    TEST_ASSERT(ctx, Leargas_ActiveGadget() == &g, "gadget kept focus mid-edit");

    // No window message yet — the keys were consumed by the gadget.
    struct IntuiMessage *im = nullptr;
    TEST_ASSERT(ctx, !Leargas_IDCMP_GetMsg(win, &im), "no IDCMP before Return");

    ev.ie_code = 0x44; // Return
    ev.ie_qualifier = 0;
    TEST_ASSERT(ctx, Leargas_Input_Post(&ev), "post Return");
    (void)Leargas_Input_Drain(&p);

    TEST_ASSERT(ctx, Leargas_ActiveGadget() == nullptr, "Return dropped edit focus");
    TEST_ASSERT(ctx, Leargas_IDCMP_GetMsg(win, &im), "Return delivered an IntuiMessage");
    TEST_ASSERT(ctx, im->Class == IDCMP_GADGETUP, "Class IDCMP_GADGETUP");
    TEST_ASSERT(ctx, im->Code == 7, "Code carries the GadgetID");
    TEST_ASSERT(ctx, im->IAddress == &g, "IAddress points at the gadget");
    Leargas_IDCMP_DisposeMsg(im);

    Leargas_CloseWindow(win);
    Leargas_CloseScreen(scr);
    Dath_FreeBitmap(&save);
    Dath_FreeBitmap(&fb);
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);
}
