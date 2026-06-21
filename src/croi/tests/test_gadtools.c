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
#include <cara/leargas.h>
#include <cara/test.h>
#include <cara/types.h>
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
