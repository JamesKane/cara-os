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
#include <cara/graphics_lib.h> // Croi_Gfx_* (DrawBevelBoxA)
#include <cara/leargas.h>      // Leargas_Gadget_Render
#include <cara/shared.h>       // Croi_AllocShared
#include <cara/tagitem.h>      // Croi_GetTagData
#include <cara/types.h>
#include <exec/types.h>
#include <graphics/rastport.h>
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

// Copy at most max-1 bytes of src into dst, NUL-terminating. src may be
// null (→ empty). Returns the copied length.
static int gt_strcopy(char *dst, const char *src, int max)
{
    int n = 0;
    if (src) {
        while (src[n] && n < max - 1) {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = '\0';
    return n;
}

// Parse a leading signed decimal from s. No validation beyond digits.
static LONG gt_parse_int(const char *s)
{
    if (!s) {
        return 0;
    }
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    LONG v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

// Count a NULL-terminated array of string pointers (CYCLE/MX labels).
static UWORD gt_count_labels(const char **labels)
{
    UWORD n = 0;
    if (labels) {
        while (labels[n]) {
            n++;
        }
    }
    return n;
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
    case CYCLE_KIND:
        g->GadgetType = GTYP_BOOLGADGET;
        g->Activation = GACT_RELVERIFY; // click advances (GT_GetIMsg, L8.4)
        ext->labels = (const char **)(uptr)Croi_GetTagData(tags, GTCY_Labels, 0);
        ext->nlabels = gt_count_labels(ext->labels);
        ext->active = (UWORD)Croi_GetTagData(tags, GTCY_Active, 0);
        if (ext->nlabels && ext->active >= ext->nlabels) {
            ext->active = 0;
        }
        labeltext = (ext->labels && ext->nlabels) ? ext->labels[ext->active] : nullptr;
        break;
    case MX_KIND:
        // v0: a single-field radio showing the active label (full
        // multi-option render is a later refinement).
        g->GadgetType = GTYP_BOOLGADGET;
        g->Activation = GACT_RELVERIFY;
        ext->labels = (const char **)(uptr)Croi_GetTagData(tags, GTMX_Labels, 0);
        ext->nlabels = gt_count_labels(ext->labels);
        ext->active = (UWORD)Croi_GetTagData(tags, GTMX_Active, 0);
        if (ext->nlabels && ext->active >= ext->nlabels) {
            ext->active = 0;
        }
        labeltext = (ext->labels && ext->nlabels) ? ext->labels[ext->active] : nullptr;
        break;
    case STRING_KIND:
    case INTEGER_KIND: {
        // GTYP_STRGADGET → the Leargas string editor; SpecialInfo is the
        // StringInfo at ext offset 0.
        g->GadgetType = GTYP_STRGADGET;
        g->Activation = GACT_RELVERIFY | (kind == INTEGER_KIND ? GACT_LONGINT : 0);
        WORD maxc = (WORD)Croi_GetTagData(
            tags, kind == INTEGER_KIND ? GTIN_MaxChars : GTST_MaxChars, CARA_GT_STRBUF - 1);
        if (maxc <= 0 || maxc >= CARA_GT_STRBUF) {
            maxc = CARA_GT_STRBUF - 1;
        }
        if (kind == INTEGER_KIND) {
            ext->number = (LONG)Croi_GetTagData(tags, GTIN_Number, 0);
            gt_format_number(ext->strbuf, ext->number);
        } else {
            gt_strcopy(ext->strbuf, (const char *)(uptr)Croi_GetTagData(tags, GTST_String, 0),
                       CARA_GT_STRBUF);
        }
        ext->sinfo.Buffer = (UBYTE *)ext->strbuf;
        ext->sinfo.UndoBuffer = (UBYTE *)ext->undobuf;
        ext->sinfo.MaxChars = (WORD)(maxc + 1); // includes the NUL (V36)
        int len = 0;
        while (ext->strbuf[len]) {
            len++;
        }
        ext->sinfo.NumChars = (WORD)len;
        ext->sinfo.BufferPos = (WORD)len;
        labeltext = nullptr; // the field shows the buffer, not GadgetText
        break;
    }
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
    case CYCLE_KIND:
        ext->active = (UWORD)Croi_GetTagData(tags, GTCY_Active, ext->active);
        if (ext->nlabels && ext->active >= ext->nlabels) {
            ext->active = 0;
        }
        ext->label.IText = (ext->labels && ext->nlabels) ? (UBYTE *)(uptr)ext->labels[ext->active]
                                                         : nullptr;
        break;
    case MX_KIND:
        ext->active = (UWORD)Croi_GetTagData(tags, GTMX_Active, ext->active);
        if (ext->nlabels && ext->active >= ext->nlabels) {
            ext->active = 0;
        }
        ext->label.IText = (ext->labels && ext->nlabels) ? (UBYTE *)(uptr)ext->labels[ext->active]
                                                         : nullptr;
        break;
    case STRING_KIND: {
        const char *s = (const char *)(uptr)Croi_GetTagData(tags, GTST_String, 0);
        if (s) {
            ext->sinfo.NumChars = (WORD)gt_strcopy(ext->strbuf, s, CARA_GT_STRBUF);
            ext->sinfo.BufferPos = ext->sinfo.NumChars;
        }
        break;
    }
    case INTEGER_KIND: {
        IPTR n = Croi_GetTagData(tags, GTIN_Number, (IPTR)ext->number);
        ext->number = (LONG)n;
        gt_format_number(ext->strbuf, ext->number);
        int len = 0;
        while (ext->strbuf[len]) {
            len++;
        }
        ext->sinfo.NumChars = (WORD)len;
        ext->sinfo.BufferPos = (WORD)len;
        break;
    }
    default:
        break;
    }
    if (win) {
        Leargas_Gadget_Render(win, gad);
    }
}

// DrawBevelBoxA(rp, l, t, w, h, tags): a raised (or GTBB_Recessed) bevel
// box over the RastPort — top+left in the shine pen, bottom+right in the
// shadow pen. v0 uses the default DrawInfo pens (shine=1/white,
// shadow=0/black) directly via the graphics RastPort primitives. (Note:
// this updates the RastPort's pen state, like the V36 call.)
void Croi_GT_DrawBevelBoxA_Impl(struct RastPort *rp, WORD left, WORD top, WORD width, WORD height,
                                struct TagItem *tags)
{
    if (!rp || width <= 0 || height <= 0) {
        return;
    }
    ULONG shine = 1;  // SHINEPEN → palette white in the v0 default map
    ULONG shadow = 0; // SHADOWPEN → palette black
    if (Croi_GetTagData(tags, GTBB_Recessed, 0)) {
        ULONG t = shine;
        shine = shadow;
        shadow = t;
    }
    WORD r = (WORD)(left + width - 1);
    WORD b = (WORD)(top + height - 1);

    // shine: left edge up + top edge across.
    Croi_Gfx_SetAPen_Impl(rp, shine);
    Croi_Gfx_Move_Impl(rp, left, b);
    Croi_Gfx_Draw_Impl(rp, left, top);
    Croi_Gfx_Draw_Impl(rp, r, top);

    // shadow: right edge down + bottom edge back.
    Croi_Gfx_SetAPen_Impl(rp, shadow);
    Croi_Gfx_Move_Impl(rp, r, top);
    Croi_Gfx_Draw_Impl(rp, r, b);
    Croi_Gfx_Draw_Impl(rp, left, b);
}

// GT_GetGadgetAttrsA(gad, win, req, tags): read back the gadget's kind
// attributes. Each understood tag's ti_Data is a pointer to caller
// storage (the GetAttr idiom); we fetch the storage via Croi_GetTagData
// and write the value. Returns the count of attributes understood.
ULONG Croi_GT_GetGadgetAttrsA_Impl(struct Gadget *gad, struct Window *win, struct Requester *req,
                                   struct TagItem *tags)
{
    (void)win;
    (void)req;
    if (!gad || !gad->SpecialInfo) {
        return 0;
    }
    struct GtGadgetExt *ext = (struct GtGadgetExt *)gad->SpecialInfo;
    ULONG n = 0;

    switch (ext->kind) {
    case CHECKBOX_KIND: {
        IPTR p = Croi_GetTagData(tags, GTCB_Checked, 0);
        if (p) {
            *(ULONG *)(uptr)p = (gad->Flags & GFLG_SELECTED) ? 1 : 0;
            n++;
        }
        break;
    }
    case CYCLE_KIND: {
        IPTR p = Croi_GetTagData(tags, GTCY_Active, 0);
        if (p) {
            *(ULONG *)(uptr)p = ext->active;
            n++;
        }
        break;
    }
    case MX_KIND: {
        IPTR p = Croi_GetTagData(tags, GTMX_Active, 0);
        if (p) {
            *(ULONG *)(uptr)p = ext->active;
            n++;
        }
        break;
    }
    case NUMBER_KIND: {
        IPTR p = Croi_GetTagData(tags, GTNM_Number, 0);
        if (p) {
            *(LONG *)(uptr)p = ext->number;
            n++;
        }
        break;
    }
    case STRING_KIND: {
        IPTR p = Croi_GetTagData(tags, GTST_String, 0);
        if (p) {
            *(STRPTR *)(uptr)p = (STRPTR)ext->sinfo.Buffer;
            n++;
        }
        break;
    }
    case INTEGER_KIND: {
        IPTR p = Croi_GetTagData(tags, GTIN_Number, 0);
        if (p) {
            *(LONG *)(uptr)p = gt_parse_int(ext->strbuf);
            n++;
        }
        break;
    }
    default:
        break;
    }
    return n;
}
