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
#include <cara/leargas.h> // Leargas_Gadget_Render
#include <cara/shared.h>  // Croi_AllocShared
#include <cara/tagitem.h> // Croi_GetTagData
#include <cara/types.h>
#include <exec/types.h>
#include <intuition/intuition.h> // struct Gadget, GTYP_*, GACT_*, GFLG_*
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

// Format a signed decimal into buf (NUL-terminated). Freestanding — no
// libc. buf must hold at least 12 chars (INT32 range + sign + NUL).
static void gt_format_number(char *buf, LONG n)
{
    char tmp[12];
    int i = 0;
    u32 mag = (n < 0) ? (u32)(-(i64)n) : (u32)n;
    do {
        tmp[i++] = (char)('0' + (mag % 10));
        mag /= 10;
    } while (mag);
    int j = 0;
    if (n < 0) {
        buf[j++] = '-';
    }
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
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

// CreateGadgetA(kind, prevGad, ng, tags): build a gadtools gadget, fill it
// from the NewGadget + the kind's GT*_* tags, and chain it after prevGad.
// Returns the new gadget (the next prevGad), or nullptr on failure — the
// V36 contract: a prior failure (prevGad == nullptr) short-circuits, and
// FreeGadgets on the context list cleans up the partial chain.
struct Gadget *Croi_GT_CreateGadgetA_Impl(ULONG kind, struct Gadget *prevGad, struct NewGadget *ng,
                                          struct TagItem *tags)
{
    if (!prevGad || !ng) {
        return nullptr;
    }
    struct GtGadgetExt *ext = (struct GtGadgetExt *)Croi_AllocShared(sizeof(struct GtGadgetExt));
    if (!ext) {
        return nullptr;
    }
    *ext = (struct GtGadgetExt){ 0 };
    ext->kind = (UWORD)kind;

    struct Gadget *g = (struct Gadget *)Croi_AllocShared(sizeof(struct Gadget));
    if (!g) {
        Croi_Free(ext);
        return nullptr;
    }
    *g = (struct Gadget){ 0 };
    g->LeftEdge = ng->ng_LeftEdge;
    g->TopEdge = ng->ng_TopEdge;
    g->Width = ng->ng_Width;
    g->Height = ng->ng_Height;
    g->GadgetID = ng->ng_GadgetID;
    g->UserData = ng->ng_UserData;
    g->SpecialInfo = ext;

    // The label IntuiText (inside the ext); GadgetText points at it.
    ext->label.FrontPen = 0; // TEXTPEN-ish; the Leargas renderer recolours
    ext->label.BackPen = 1;
    ext->label.DrawMode = 0; // JAM1
    ext->label.LeftEdge = 4;
    ext->label.TopEdge = 2;
    g->GadgetText = &ext->label;

    const char *labeltext = ng->ng_GadgetText;
    switch (kind) {
    case BUTTON_KIND:
        g->GadgetType = GTYP_BOOLGADGET;
        g->Activation = GACT_RELVERIFY; // click → IDCMP_GADGETUP
        break;
    case CHECKBOX_KIND:
        g->GadgetType = GTYP_BOOLGADGET;
        g->Activation = GACT_RELVERIFY | GACT_TOGGLESELECT;
        if (Croi_GetTagData(tags, GTCB_Checked, 0)) {
            g->Flags |= GFLG_SELECTED;
        }
        break;
    case TEXT_KIND:
        g->GadgetType = GTYP_BOOLGADGET; // display only (no RELVERIFY)
        labeltext = (const char *)(uptr)Croi_GetTagData(tags, GTTX_Text,
                                                        (IPTR)(uptr)ng->ng_GadgetText);
        break;
    case NUMBER_KIND:
        g->GadgetType = GTYP_BOOLGADGET; // display only
        ext->number = (LONG)Croi_GetTagData(tags, GTNM_Number, 0);
        gt_format_number(ext->numbuf, ext->number);
        labeltext = ext->numbuf;
        break;
    default:
        g->GadgetType = GTYP_BOOLGADGET; // GENERIC / unsupported kind in v0
        break;
    }
    ext->label.IText = (UBYTE *)(uptr)labeltext;

    // Chain after prevGad (append at its tail, normally null).
    g->NextGadget = prevGad->NextGadget;
    prevGad->NextGadget = g;
    return g;
}

// GT_SetGadgetAttrsA(gad, win, req, tags): apply post-create attribute
// changes for the gadget's kind, then re-render if a window is given.
// Tags default to the current value (absent tag → no change).
void Croi_GT_SetGadgetAttrsA_Impl(struct Gadget *gad, struct Window *win, struct Requester *req,
                                  struct TagItem *tags)
{
    (void)req;
    if (!gad || !gad->SpecialInfo) {
        return;
    }
    struct GtGadgetExt *ext = (struct GtGadgetExt *)gad->SpecialInfo;
    switch (ext->kind) {
    case CHECKBOX_KIND: {
        IPTR cur = (gad->Flags & GFLG_SELECTED) ? 1 : 0;
        if (Croi_GetTagData(tags, GTCB_Checked, cur)) {
            gad->Flags |= GFLG_SELECTED;
        } else {
            gad->Flags &= ~(UWORD)GFLG_SELECTED;
        }
        break;
    }
    case NUMBER_KIND:
        ext->number = (LONG)Croi_GetTagData(tags, GTNM_Number, (IPTR)ext->number);
        gt_format_number(ext->numbuf, ext->number);
        ext->label.IText = (UBYTE *)(uptr)ext->numbuf;
        break;
    case TEXT_KIND:
        ext->label.IText = (UBYTE *)(uptr)Croi_GetTagData(tags, GTTX_Text,
                                                          (IPTR)(uptr)ext->label.IText);
        break;
    default:
        break;
    }
    if (win) {
        Leargas_Gadget_Render(win, gad);
    }
}
