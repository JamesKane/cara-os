// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(gadtools_visualinfo): L8.1 — gadtools.library render
// context (docs/LEARGAS_GADTOOLS.md). Opens an off-screen Leargas
// screen, then drives the gadtools render-context impls directly
// (they are `syscall` flavour, so callable from the S-mode runner):
// GetVisualInfoA builds a shared-heap VisualInfo with a default
// DrawInfo; CreateContext heads a gadget list; FreeGadgets +
// FreeVisualInfo tear down.

#include <cara/dath.h>
#include <cara/gadtools_lib.h>
#include <cara/intuition_lib.h>
#include <cara/leargas.h>
#include <cara/test.h>
#include <cara/types.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <utility/tagitem.h>

KERNEL_TEST(gadtools_visualinfo)
{
    // An off-screen "screen" to derive a VisualInfo from.
    static u32 pixels[64 * 48];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 64, 48, 64 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "wb", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");

    // GetVisualInfoA → a VisualInfo with a populated default DrawInfo.
    struct CaraVisualInfo *vi = (struct CaraVisualInfo *)Croi_GT_GetVisualInfoA_Impl(scr, nullptr);
    TEST_ASSERT(ctx, vi != nullptr && vi->screen == scr, "GetVisualInfoA");
    TEST_ASSERT(ctx, vi->dri.dri_NumPens == NUMDRIPENS && vi->dri.dri_Pens == vi->pens,
                "DrawInfo pens wired");
    TEST_ASSERT(ctx, vi->dri.dri_Pens[SHINEPEN] == 1 && vi->dri.dri_Pens[SHADOWPEN] == 0,
                "default pen map");
    TEST_ASSERT(ctx, Croi_GT_GetVisualInfoA_Impl(nullptr, nullptr) == nullptr,
                "GetVisualInfoA(null) fails");

    // CreateContext → a context gadget that heads the (empty) glist.
    struct Gadget *glist = (struct Gadget *)(uptr)0xDEAD; // poisoned
    struct Gadget *ctxg = Croi_GT_CreateContext_Impl(&glist);
    TEST_ASSERT(ctx, ctxg != nullptr && glist == ctxg, "CreateContext");
    TEST_ASSERT(ctx, ctxg->NextGadget == nullptr && ctxg->GadgetType == GTYP_GADGET0002,
                "context gadget marked + empty");

    // FreeGadgets walks + frees the whole list; returns nullptr.
    TEST_ASSERT(ctx, Croi_GT_FreeGadgets_Impl(glist) == nullptr, "FreeGadgets");

    Croi_GT_FreeVisualInfo_Impl(vi);

    Leargas_CloseScreen(scr);
}

// L8.2 — the gadget factory + easy kinds + GT_SetGadgetAttrsA.
KERNEL_TEST(gadtools_creategadget)
{
    static u32 pixels[64 * 48];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 64, 48, 64 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "wb", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");

    struct CaraVisualInfo *vi = (struct CaraVisualInfo *)Croi_GT_GetVisualInfoA_Impl(scr, nullptr);
    TEST_ASSERT(ctx, vi != nullptr, "GetVisualInfoA");

    struct Gadget *glist = nullptr;
    struct Gadget *gad = Croi_GT_CreateContext_Impl(&glist);
    TEST_ASSERT(ctx, gad != nullptr, "CreateContext");

    struct NewGadget ng = {
        .ng_LeftEdge = 5,
        .ng_TopEdge = 6,
        .ng_Width = 40,
        .ng_Height = 12,
        .ng_GadgetText = (STRPTR) "OK",
        .ng_GadgetID = 100,
        .ng_VisualInfo = vi,
    };

    // BUTTON: bool gadget, RELVERIFY, label from ng_GadgetText.
    struct Gadget *gbtn = Croi_GT_CreateGadgetA_Impl(BUTTON_KIND, gad, &ng, nullptr);
    TEST_ASSERT(ctx, gbtn != nullptr, "CreateGadget BUTTON");
    TEST_ASSERT(ctx, gbtn->GadgetID == 100 && gbtn->Width == 40 && gbtn->LeftEdge == 5,
                "BUTTON geometry/id");
    TEST_ASSERT(ctx,
                (gbtn->GadgetType & GTYP_GTYPEMASK) == GTYP_BOOLGADGET &&
                    (gbtn->Activation & GACT_RELVERIFY),
                "BUTTON type/activation");
    TEST_ASSERT(ctx,
                gbtn->GadgetText && gbtn->GadgetText->IText && gbtn->GadgetText->IText[0] == 'O' &&
                    gbtn->GadgetText->IText[1] == 'K',
                "BUTTON label");
    TEST_ASSERT(ctx, gad->NextGadget == gbtn, "BUTTON chained after context");

    // CHECKBOX: GTCB_Checked TRUE → GFLG_SELECTED set.
    struct TagItem cb_tags[] = { { GTCB_Checked, 1 }, { TAG_END, 0 } };
    struct Gadget *gcb = Croi_GT_CreateGadgetA_Impl(CHECKBOX_KIND, gbtn, &ng, cb_tags);
    TEST_ASSERT(ctx, gcb != nullptr && (gcb->Flags & GFLG_SELECTED), "CHECKBOX checked");
    TEST_ASSERT(ctx, gbtn->NextGadget == gcb, "CHECKBOX chained");

    // NUMBER: GTNM_Number 42 → label "42".
    struct TagItem num_tags[] = { { GTNM_Number, 42 }, { TAG_END, 0 } };
    struct Gadget *gnum = Croi_GT_CreateGadgetA_Impl(NUMBER_KIND, gcb, &ng, num_tags);
    TEST_ASSERT(ctx, gnum != nullptr && gnum->GadgetText, "CreateGadget NUMBER");
    TEST_ASSERT(ctx,
                gnum->GadgetText->IText[0] == '4' && gnum->GadgetText->IText[1] == '2' &&
                    gnum->GadgetText->IText[2] == 0,
                "NUMBER label formatted");

    // TEXT: GTTX_Text → display label.
    struct TagItem tx_tags[] = { { GTTX_Text, (IPTR)(uptr) "hi" }, { TAG_END, 0 } };
    struct Gadget *gtxt = Croi_GT_CreateGadgetA_Impl(TEXT_KIND, gnum, &ng, tx_tags);
    TEST_ASSERT(ctx, gtxt != nullptr && gtxt->GadgetText->IText[0] == 'h', "TEXT label");

    // GT_SetGadgetAttrsA: clear the checkbox, change the number.
    struct TagItem uncheck[] = { { GTCB_Checked, 0 }, { TAG_END, 0 } };
    Croi_GT_SetGadgetAttrsA_Impl(gcb, nullptr, nullptr, uncheck);
    TEST_ASSERT(ctx, !(gcb->Flags & GFLG_SELECTED), "SetGadgetAttrs uncheck");
    struct TagItem renum[] = { { GTNM_Number, 7 }, { TAG_END, 0 } };
    Croi_GT_SetGadgetAttrsA_Impl(gnum, nullptr, nullptr, renum);
    TEST_ASSERT(ctx, gnum->GadgetText->IText[0] == '7' && gnum->GadgetText->IText[1] == 0,
                "SetGadgetAttrs renumber");

    // FreeGadgets frees the whole list (context + 4 gadgets + their ext).
    TEST_ASSERT(ctx, Croi_GT_FreeGadgets_Impl(glist) == nullptr, "FreeGadgets");
    Croi_GT_FreeVisualInfo_Impl(vi);
    Leargas_CloseScreen(scr);
}

// L8.3 — CYCLE/MX/STRING/INTEGER kinds + GT_GetGadgetAttrsA + DrawBevelBoxA.
KERNEL_TEST(gadtools_kinds)
{
    static u32 pixels[64 * 48];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 64, 48, 64 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    struct Screen *scr = Leargas_OpenScreen(&fb, "wb", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");
    struct CaraVisualInfo *vi = (struct CaraVisualInfo *)Croi_GT_GetVisualInfoA_Impl(scr, nullptr);
    TEST_ASSERT(ctx, vi != nullptr, "GetVisualInfoA");

    struct Gadget *glist = nullptr;
    struct Gadget *prev = Croi_GT_CreateContext_Impl(&glist);
    TEST_ASSERT(ctx, prev != nullptr, "CreateContext");

    struct NewGadget ng = {
        .ng_LeftEdge = 4, .ng_TopEdge = 4, .ng_Width = 50, .ng_Height = 12, .ng_VisualInfo = vi
    };

    // CYCLE: 3 labels, start at index 1 ("B").
    static const char *cyc[] = { "A", "B", "C", nullptr };
    struct TagItem cy_tags[] = { { GTCY_Labels, (IPTR)(uptr)cyc },
                                 { GTCY_Active, 1 },
                                 { TAG_END, 0 } };
    struct Gadget *gcy = Croi_GT_CreateGadgetA_Impl(CYCLE_KIND, prev, &ng, cy_tags);
    struct GtGadgetExt *cye = (struct GtGadgetExt *)gcy->SpecialInfo;
    TEST_ASSERT(ctx, cye->kind == CYCLE_KIND && cye->nlabels == 3 && cye->active == 1,
                "CYCLE setup");
    TEST_ASSERT(ctx, gcy->GadgetText->IText[0] == 'B', "CYCLE shows active label");
    struct TagItem cy_set[] = { { GTCY_Active, 2 }, { TAG_END, 0 } };
    Croi_GT_SetGadgetAttrsA_Impl(gcy, nullptr, nullptr, cy_set);
    TEST_ASSERT(ctx, cye->active == 2 && gcy->GadgetText->IText[0] == 'C', "CYCLE advance");
    ULONG cyget = 0;
    struct TagItem cy_get[] = { { GTCY_Active, (IPTR)(uptr)&cyget }, { TAG_END, 0 } };
    TEST_ASSERT(ctx, Croi_GT_GetGadgetAttrsA_Impl(gcy, nullptr, nullptr, cy_get) == 1 && cyget == 2,
                "CYCLE GetGadgetAttrs");
    prev = gcy;

    // MX: 2 labels, active 0.
    static const char *mxl[] = { "X", "Y", nullptr };
    struct TagItem mx_tags[] = { { GTMX_Labels, (IPTR)(uptr)mxl },
                                 { GTMX_Active, 0 },
                                 { TAG_END, 0 } };
    struct Gadget *gmx = Croi_GT_CreateGadgetA_Impl(MX_KIND, prev, &ng, mx_tags);
    struct GtGadgetExt *mxe = (struct GtGadgetExt *)gmx->SpecialInfo;
    TEST_ASSERT(ctx, mxe->nlabels == 2 && mxe->active == 0 && gmx->GadgetText->IText[0] == 'X',
                "MX setup");
    prev = gmx;

    // STRING: GTYP_STRGADGET, SpecialInfo IS a StringInfo (offset 0).
    struct TagItem st_tags[] = { { GTST_String, (IPTR)(uptr) "hi" },
                                 { GTST_MaxChars, 10 },
                                 { TAG_END, 0 } };
    struct Gadget *gst = Croi_GT_CreateGadgetA_Impl(STRING_KIND, prev, &ng, st_tags);
    TEST_ASSERT(ctx, (gst->GadgetType & GTYP_GTYPEMASK) == GTYP_STRGADGET, "STRING type");
    struct StringInfo *si = (struct StringInfo *)gst->SpecialInfo; // == &ext->sinfo
    TEST_ASSERT(ctx,
                si->Buffer && si->Buffer[0] == 'h' && si->Buffer[1] == 'i' && si->NumChars == 2,
                "STRING buffer");
    struct TagItem st_set[] = { { GTST_String, (IPTR)(uptr) "world" }, { TAG_END, 0 } };
    Croi_GT_SetGadgetAttrsA_Impl(gst, nullptr, nullptr, st_set);
    TEST_ASSERT(ctx, si->Buffer[0] == 'w' && si->NumChars == 5, "STRING SetGadgetAttrs");
    STRPTR gotstr = nullptr;
    struct TagItem st_get[] = { { GTST_String, (IPTR)(uptr)&gotstr }, { TAG_END, 0 } };
    TEST_ASSERT(ctx,
                Croi_GT_GetGadgetAttrsA_Impl(gst, nullptr, nullptr, st_get) == 1 && gotstr &&
                    gotstr[0] == 'w',
                "STRING GetGadgetAttrs");
    prev = gst;

    // INTEGER: GTIN_Number formatted; read back parses.
    struct TagItem in_tags[] = { { GTIN_Number, 1234 }, { TAG_END, 0 } };
    struct Gadget *gin = Croi_GT_CreateGadgetA_Impl(INTEGER_KIND, prev, &ng, in_tags);
    struct StringInfo *isi = (struct StringInfo *)gin->SpecialInfo;
    TEST_ASSERT(ctx, isi->Buffer[0] == '1' && isi->Buffer[3] == '4', "INTEGER formatted");
    LONG gotnum = 0;
    struct TagItem in_get[] = { { GTIN_Number, (IPTR)(uptr)&gotnum }, { TAG_END, 0 } };
    TEST_ASSERT(ctx,
                Croi_GT_GetGadgetAttrsA_Impl(gin, nullptr, nullptr, in_get) == 1 && gotnum == 1234,
                "INTEGER GetGadgetAttrs");

    // DrawBevelBoxA over the screen's RastPort — runs + leaves the pen
    // cursor at the last drawn vertex (left,bottom).
    if (scr->RastPort) {
        Croi_GT_DrawBevelBoxA_Impl(scr->RastPort, 2, 2, 20, 10, nullptr);
        TEST_ASSERT(ctx, scr->RastPort->cp_x == 2, "DrawBevelBoxA ran (cursor at left)");
    }

    TEST_ASSERT(ctx, Croi_GT_FreeGadgets_Impl(glist) == nullptr, "FreeGadgets");
    Croi_GT_FreeVisualInfo_Impl(vi);
    Leargas_CloseScreen(scr);
}

// L8.4 — GT_GetIMsg gadget update + the menu builder.
KERNEL_TEST(gadtools_imsg_menu)
{
    static u32 pixels[80 * 60];
    struct DathFramebuffer fb;
    TEST_ASSERT(ctx,
                Dath_Framebuffer_Init(&fb, pixels, 80, 60, 80 * 4, DATH_FMT_RGBA8888) == CARA_EOK,
                "fb init");
    Leargas_SetDisplayFramebuffer(&fb);
    struct Screen *scr = Leargas_OpenScreen(&fb, "wb", 0);
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");
    struct CaraVisualInfo *vi = (struct CaraVisualInfo *)Croi_GT_GetVisualInfoA_Impl(scr, nullptr);
    TEST_ASSERT(ctx, vi != nullptr, "GetVisualInfoA");

    // A window with a UserPort listening for GADGETUP.
    struct TagItem wt[] = { { WA_Left, 4 },
                            { WA_Top, 4 },
                            { WA_Width, 60 },
                            { WA_Height, 40 },
                            { WA_IDCMP, IDCMP_GADGETUP },
                            { WA_Activate, 1 },
                            { TAG_DONE, 0 } };
    struct Window *win = Croi_OpenWindowTagList_Impl(nullptr, wt);
    TEST_ASSERT(ctx, win != nullptr && win->UserPort != nullptr, "window w/ GADGETUP port");

    struct Gadget *glist = nullptr;
    struct Gadget *prev = Croi_GT_CreateContext_Impl(&glist);

    // CYCLE at index 0; a GADGETUP advances it to 1 and rewrites Code.
    static const char *cyc[] = { "A", "B", "C", nullptr };
    struct NewGadget ng = { .ng_LeftEdge = 4,
                            .ng_TopEdge = 4,
                            .ng_Width = 50,
                            .ng_Height = 12,
                            .ng_VisualInfo = vi,
                            .ng_GadgetID = 7 };
    struct TagItem cy_tags[] = { { GTCY_Labels, (IPTR)(uptr)cyc },
                                 { GTCY_Active, 0 },
                                 { TAG_END, 0 } };
    struct Gadget *gcy = Croi_GT_CreateGadgetA_Impl(CYCLE_KIND, prev, &ng, cy_tags);
    struct GtGadgetExt *cye = (struct GtGadgetExt *)gcy->SpecialInfo;
    Leargas_AddGadget(win, gcy);

    TEST_ASSERT(ctx, Leargas_IDCMP_PostGadgetUp(win, gcy), "post CYCLE GADGETUP");
    struct IntuiMessage *im = Croi_GT_GetIMsg_Impl(win->UserPort);
    TEST_ASSERT(ctx, im != nullptr && im->Class == IDCMP_GADGETUP, "GT_GetIMsg returns msg");
    TEST_ASSERT(ctx, cye->active == 1 && im->Code == 1, "CYCLE advanced by GT_GetIMsg");
    Croi_GT_ReplyIMsg_Impl(im);
    TEST_ASSERT(ctx, Croi_GT_GetIMsg_Impl(win->UserPort) == nullptr, "GT_GetIMsg empty");

    // CHECKBOX toggles on GADGETUP.
    struct Gadget *gcb = Croi_GT_CreateGadgetA_Impl(CHECKBOX_KIND, gcy, &ng, nullptr);
    Leargas_AddGadget(win, gcb);
    TEST_ASSERT(ctx, !(gcb->Flags & GFLG_SELECTED), "checkbox starts clear");
    TEST_ASSERT(ctx, Leargas_IDCMP_PostGadgetUp(win, gcb), "post CHECKBOX GADGETUP");
    im = Croi_GT_GetIMsg_Impl(win->UserPort);
    TEST_ASSERT(ctx, im && (gcb->Flags & GFLG_SELECTED) && im->Code == 1, "CHECKBOX toggled");
    Croi_GT_ReplyIMsg_Impl(im);

    Croi_GT_RefreshWindow_Impl(win, nullptr); // no-crash re-render

    // Menu builder: a NewMenu[] → Menu/MenuItem chain, then layout.
    struct NewMenu nm[] = {
        { NM_TITLE, (STRPTR) "Project", nullptr, 0, 0, nullptr },
        { NM_ITEM, (STRPTR) "Open", nullptr, 0, 0, nullptr },
        { NM_ITEM, (STRPTR) "Quit", nullptr, 0, 0, nullptr },
        { NM_END, nullptr, nullptr, 0, 0, nullptr },
    };
    struct Menu *menu = Croi_GT_CreateMenusA_Impl(nm, nullptr);
    TEST_ASSERT(ctx, menu != nullptr && menu->MenuName && menu->MenuName[0] == 'P', "CreateMenusA");
    struct MenuItem *i0 = menu->FirstItem;
    TEST_ASSERT(ctx, i0 && i0->NextItem && !i0->NextItem->NextItem, "two items");
    struct IntuiText *it0 = (struct IntuiText *)i0->ItemFill;
    TEST_ASSERT(ctx, it0 && it0->IText && it0->IText[0] == 'O', "item label");
    TEST_ASSERT(ctx, Croi_GT_LayoutMenusA_Impl(menu, vi, nullptr) && menu->Width > 0,
                "LayoutMenusA assigns geometry");
    Croi_GT_FreeMenus_Impl(menu);

    Croi_CloseWindow_Impl(win);
    Croi_GT_FreeGadgets_Impl(glist);
    Croi_GT_FreeVisualInfo_Impl(vi);
    Leargas_CloseScreen(scr);
    Leargas_SetDisplayFramebuffer(nullptr);
}

// L8.5 — proportional gadgets (SLIDER) + router drag-tracking.
KERNEL_TEST(gadtools_prop)
{
    Leargas_SetGadgetRouter(Leargas_IDCMP_PostGadgetUp);

    struct DathFramebuffer fb;
    TEST_ASSERT(ctx, Dath_AllocBitmap(&fb, 200, 120, DATH_FMT_RGBA8888) == CARA_EOK, "fb alloc");
    Leargas_SetDisplayFramebuffer(&fb);
    struct Screen *scr = Leargas_OpenScreen(&fb, "P", Dath_RGB(0x10, 0x10, 0x20));
    TEST_ASSERT(ctx, scr != nullptr, "OpenScreen");
    struct CaraVisualInfo *vi = (struct CaraVisualInfo *)Croi_GT_GetVisualInfoA_Impl(scr, nullptr);
    TEST_ASSERT(ctx, vi != nullptr, "GetVisualInfoA");

    char wtitle[] = "Win";
    struct NewWindow nw = { .LeftEdge = 10,
                            .TopEdge = 10,
                            .Width = 150,
                            .Height = 90,
                            .Flags = WFLG_DRAGBAR | WFLG_ACTIVATE,
                            .IDCMPFlags = IDCMP_GADGETUP,
                            .Title = (UBYTE *)wtitle,
                            .Screen = scr };
    struct Window *win = Leargas_OpenWindow(&nw);
    TEST_ASSERT(ctx, win != nullptr && win->UserPort != nullptr, "OpenWindow w/ GADGETUP");

    // A SLIDER, level 0 in [0,100], at window-relative (10,30) size 100x12.
    struct Gadget *glist = nullptr;
    struct Gadget *prev = Croi_GT_CreateContext_Impl(&glist);
    struct NewGadget ng = { .ng_LeftEdge = 10,
                            .ng_TopEdge = 30,
                            .ng_Width = 100,
                            .ng_Height = 12,
                            .ng_VisualInfo = vi,
                            .ng_GadgetID = 9 };
    struct TagItem sl_tags[] = {
        { GTSL_Min, 0 }, { GTSL_Max, 100 }, { GTSL_Level, 0 }, { TAG_END, 0 }
    };
    struct Gadget *gsl = Croi_GT_CreateGadgetA_Impl(SLIDER_KIND, prev, &ng, sl_tags);
    TEST_ASSERT(ctx, gsl != nullptr && (gsl->GadgetType & GTYP_GTYPEMASK) == GTYP_PROPGADGET,
                "SLIDER is a prop gadget");
    Leargas_AddGadget(win, gsl);

    // Level 0 → pot 0 → GetGadgetAttrs reads 0.
    LONG lvl = -1;
    struct TagItem get_lvl[] = { { GTSL_Level, (IPTR)(uptr)&lvl }, { TAG_END, 0 } };
    TEST_ASSERT(ctx, Croi_GT_GetGadgetAttrsA_Impl(gsl, nullptr, nullptr, get_lvl) == 1 && lvl == 0,
                "initial level 0");

    // Direct drag to the gadget midpoint → pot ~half → level ~50.
    TEST_ASSERT(ctx, Leargas_Prop_HandleDrag(gsl, 10 + 50, 36), "HandleDrag mid changed pot");
    Croi_GT_GetGadgetAttrsA_Impl(gsl, nullptr, nullptr, get_lvl);
    TEST_ASSERT(ctx, lvl >= 45 && lvl <= 55, "mid drag → level ~50");

    // SetGadgetAttrs(level) round-trips back through the pot.
    struct TagItem set_lvl[] = { { GTSL_Level, 80 }, { TAG_END, 0 } };
    Croi_GT_SetGadgetAttrsA_Impl(gsl, nullptr, nullptr, set_lvl);
    Croi_GT_GetGadgetAttrsA_Impl(gsl, nullptr, nullptr, get_lvl);
    TEST_ASSERT(ctx, lvl >= 78 && lvl <= 82, "SetGadgetAttrs level 80");

    // ---- Full router drag: click left of the knob, drag right, release. ----
    struct DathFramebuffer save;
    TEST_ASSERT(ctx,
                Dath_AllocBitmap(&save, leargas_pointer_arrow.width, leargas_pointer_arrow.height,
                                 fb.format) == CARA_EOK,
                "pointer save alloc");
    // Start the pointer over the gadget's left end.
    i32 px = win->LeftEdge + gsl->LeftEdge + 2;
    i32 py = win->TopEdge + gsl->TopEdge + 6;
    struct LeargasPointer p;
    TEST_ASSERT(ctx,
                Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow,
                                     Dath_RGB(0xFF, 0xFF, 0xFF), Dath_RGB(0, 0, 0), px,
                                     py) == CARA_EOK,
                "pointer init");
    Leargas_SetActiveWindow(win);

    struct LeargasInputEvent ev = { .ie_class = IECLASS_RAWMOUSE };
    ev.ie_code = IECODE_LBUTTON; // press over the left of the slider
    TEST_ASSERT(ctx, Leargas_Input_Post(&ev), "post LBUTTON down");
    ev.ie_code = IECODE_NOBUTTON; // drag right ~90px
    ev.ie_dx = 90;
    TEST_ASSERT(ctx, Leargas_Input_Post(&ev), "post drag right");
    ev.ie_code = IECODE_LBUTTON | IECODE_UP_PREFIX; // release
    ev.ie_dx = 0;
    TEST_ASSERT(ctx, Leargas_Input_Post(&ev), "post LBUTTON up");
    (void)Leargas_Input_Drain(&p);

    // The drag pushed the knob to the right → a high level, and release
    // posted IDCMP_GADGETUP for the slider.
    Croi_GT_GetGadgetAttrsA_Impl(gsl, nullptr, nullptr, get_lvl);
    TEST_ASSERT(ctx, lvl >= 85, "drag-right raised the level");
    struct IntuiMessage *im = Croi_GT_GetIMsg_Impl(win->UserPort);
    TEST_ASSERT(ctx, im != nullptr && im->Class == IDCMP_GADGETUP, "release posted GADGETUP");
    Croi_GT_ReplyIMsg_Impl(im);

    Croi_CloseWindow_Impl(win);
    Croi_GT_FreeGadgets_Impl(glist);
    Croi_GT_FreeVisualInfo_Impl(vi);
    Leargas_CloseScreen(scr);
    Leargas_SetDisplayFramebuffer(nullptr);
}
