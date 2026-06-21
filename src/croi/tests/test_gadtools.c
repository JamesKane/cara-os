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
