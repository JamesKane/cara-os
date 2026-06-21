// SPDX-License-Identifier: BSD-2-Clause
//
// gadtools.library render context (L8.1, docs/LEARGAS_GADTOOLS.md §2.3-4):
// GetVisualInfoA/FreeVisualInfo, CreateContext/FreeGadgets. All `syscall`
// flavour — the kernel allocates the VisualInfo / context gadget in the
// SASOS shared heap (U-mode-visible) and returns the pointer. gadtools
// builds plain struct Gadgets over the Leargas substrate, so the gadget
// kinds (L8.2+) read pens/font from the VisualInfo's DrawInfo.

#include <cara/alloc.h> // Croi_Free
#include <cara/gadtools_lib.h>
#include <cara/shared.h> // Croi_AllocShared
#include <cara/types.h>
#include <exec/types.h>
#include <intuition/intuition.h> // struct Gadget, GTYP_*
#include <intuition/screens.h>
#include <libraries/gadtools.h>

// v0 default DrawInfo pen map over the L4.2 8-entry palette
// (0=blk 1=wht 2=red 3=grn 4=blu 5=cyan 6=mag 7=yel): a simple
// black-on-white look with blue fill for selection.
static const UWORD g_default_pens[NUMDRIPENS] = {
    [DETAILPEN] = 0,   [BLOCKPEN] = 1,      [TEXTPEN] = 0,
    [SHINEPEN] = 1,    [SHADOWPEN] = 0,     [FILLPEN] = 4,
    [FILLTEXTPEN] = 1, [BACKGROUNDPEN] = 1, [HIGHLIGHTTEXTPEN] = 0,
};

// GetVisualInfoA(screen, tags): build a shared-heap VisualInfo carrying
// the screen and a default DrawInfo. tags are ignored in v0.
APTR Croi_GT_GetVisualInfoA_Impl(struct Screen *screen, struct TagItem *tags)
{
    (void)tags;
    if (!screen) {
        return nullptr;
    }
    struct CaraVisualInfo *vi =
        (struct CaraVisualInfo *)Croi_AllocShared(sizeof(struct CaraVisualInfo));
    if (!vi) {
        return nullptr;
    }
    vi->screen = screen;
    for (int i = 0; i < NUMDRIPENS; i++) {
        vi->pens[i] = g_default_pens[i];
    }
    vi->dri = (struct DrawInfo){ 0 };
    vi->dri.dri_Version = DRI_VERSION;
    vi->dri.dri_NumPens = NUMDRIPENS;
    vi->dri.dri_Pens = vi->pens;
    vi->dri.dri_Depth = screen->BitMap ? (UWORD)8 : (UWORD)8; // v0: chunky
    vi->dri.dri_Resolution.X = 1;
    vi->dri.dri_Resolution.Y = 1;
    return vi;
}

void Croi_GT_FreeVisualInfo_Impl(APTR vi)
{
    if (vi) {
        Croi_Free(vi);
    }
}

// CreateContext(glistptr): allocate the placeholder context gadget that
// heads a gadtools gadget list. It carries no imagery; CreateGadgetA
// chains the first real gadget after it, and the app AddGLists the chain
// (skipping or including the context — intuition tolerates the marker).
struct Gadget *Croi_GT_CreateContext_Impl(struct Gadget **glistptr)
{
    if (!glistptr) {
        return nullptr;
    }
    struct Gadget *ctx = (struct Gadget *)Croi_AllocShared(sizeof(struct Gadget));
    if (!ctx) {
        *glistptr = nullptr;
        return nullptr;
    }
    *ctx = (struct Gadget){ 0 };
    ctx->GadgetType = GTYP_GADGET0002; // gadtools context marker
    *glistptr = ctx;
    return ctx;
}

// FreeGadgets(glist): walk the NextGadget chain freeing each gadget and
// its SpecialInfo. Always returns nullptr (the V36 idiom: callers null
// their glist). Safe on nullptr.
struct Gadget *Croi_GT_FreeGadgets_Impl(struct Gadget *glist)
{
    struct Gadget *g = glist;
    while (g) {
        struct Gadget *next = g->NextGadget;
        if (g->SpecialInfo) {
            Croi_Free(g->SpecialInfo);
        }
        Croi_Free(g);
        g = next;
    }
    return nullptr;
}
