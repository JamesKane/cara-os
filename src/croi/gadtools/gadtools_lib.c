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
#include <cara/leargas.h>      // Leargas_Gadget_Render, Leargas_Menu_Layout, IDCMP
#include <cara/msgport.h>      // Croi_GetMsg (GT_GetIMsg)
#include <cara/ring.h>         // struct RingSlot
#include <cara/shared.h>       // Croi_AllocShared
#include <cara/tagitem.h>      // Croi_GetTagData
#include <cara/types.h>
#include <exec/ports.h>
#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h> // struct Gadget, GTYP_*, GACT_*, GFLG_*, Menu
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

// SLIDER level (min..max) <-> prop pot (0..MAXPOT).
static UWORD gt_level_to_pot(LONG level, LONG min, LONG max)
{
    if (max <= min) {
        return 0;
    }
    if (level < min) {
        level = min;
    }
    if (level > max) {
        level = max;
    }
    return (UWORD)(((u32)(level - min) * MAXPOT) / (u32)(max - min));
}

static LONG gt_pot_to_level(UWORD pot, LONG min, LONG max)
{
    if (max <= min) {
        return min;
    }
    return min + (LONG)(((u32)pot * (u32)(max - min)) / MAXPOT);
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
        ext->si.sinfo.Buffer = (UBYTE *)ext->strbuf;
        ext->si.sinfo.UndoBuffer = (UBYTE *)ext->undobuf;
        ext->si.sinfo.MaxChars = (WORD)(maxc + 1); // includes the NUL (V36)
        int len = 0;
        while (ext->strbuf[len]) {
            len++;
        }
        ext->si.sinfo.NumChars = (WORD)len;
        ext->si.sinfo.BufferPos = (WORD)len;
        labeltext = nullptr; // the field shows the buffer, not GadgetText
        break;
    }
    case SLIDER_KIND: {
        // A horizontal prop gadget; GTSL_Level in [Min,Max] → HorizPot.
        g->GadgetType = GTYP_PROPGADGET;
        g->Activation = GACT_RELVERIFY;
        ext->sl_min = (LONG)Croi_GetTagData(tags, GTSL_Min, 0);
        ext->sl_max = (LONG)Croi_GetTagData(tags, GTSL_Max, 15);
        if (ext->sl_max <= ext->sl_min) {
            ext->sl_max = ext->sl_min + 1;
        }
        LONG level = (LONG)Croi_GetTagData(tags, GTSL_Level, ext->sl_min);
        struct PropInfo *pi = &ext->si.pinfo;
        pi->Flags = AUTOKNOB | FREEHORIZ;
        pi->VertBody = MAXBODY;
        pi->HorizBody = (UWORD)(MAXBODY / (u32)(ext->sl_max - ext->sl_min + 1));
        pi->HorizPot = gt_level_to_pot(level, ext->sl_min, ext->sl_max);
        labeltext = nullptr;
        break;
    }
    case SCROLLER_KIND: {
        // GTSC_Top/Total/Visible → HorizPot + HorizBody.
        g->GadgetType = GTYP_PROPGADGET;
        g->Activation = GACT_RELVERIFY;
        ext->sc_total = (LONG)Croi_GetTagData(tags, GTSC_Total, 1);
        ext->sc_visible = (LONG)Croi_GetTagData(tags, GTSC_Visible, 1);
        if (ext->sc_total < 1) {
            ext->sc_total = 1;
        }
        if (ext->sc_visible < 1) {
            ext->sc_visible = 1;
        }
        LONG top = (LONG)Croi_GetTagData(tags, GTSC_Top, 0);
        LONG range = ext->sc_total - ext->sc_visible;
        struct PropInfo *pi = &ext->si.pinfo;
        pi->Flags = AUTOKNOB | FREEHORIZ;
        pi->VertBody = MAXBODY;
        pi->HorizBody = (UWORD)(((u32)ext->sc_visible * MAXBODY) / (u32)ext->sc_total);
        pi->HorizPot = (range > 0) ? (UWORD)(((u32)top * MAXPOT) / (u32)range) : 0;
        labeltext = nullptr;
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
            ext->si.sinfo.NumChars = (WORD)gt_strcopy(ext->strbuf, s, CARA_GT_STRBUF);
            ext->si.sinfo.BufferPos = ext->si.sinfo.NumChars;
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
        ext->si.sinfo.NumChars = (WORD)len;
        ext->si.sinfo.BufferPos = (WORD)len;
        break;
    }
    case SLIDER_KIND: {
        LONG lvl = (LONG)Croi_GetTagData(
            tags, GTSL_Level,
            (IPTR)gt_pot_to_level(ext->si.pinfo.HorizPot, ext->sl_min, ext->sl_max));
        ext->si.pinfo.HorizPot = gt_level_to_pot(lvl, ext->sl_min, ext->sl_max);
        break;
    }
    case SCROLLER_KIND: {
        LONG range = ext->sc_total - ext->sc_visible;
        LONG curtop = (range > 0) ? (LONG)(((u32)ext->si.pinfo.HorizPot * (u32)range) / MAXPOT) : 0;
        LONG top = (LONG)Croi_GetTagData(tags, GTSC_Top, (IPTR)curtop);
        ext->si.pinfo.HorizPot = (range > 0) ? (UWORD)(((u32)top * MAXPOT) / (u32)range) : 0;
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
            *(STRPTR *)(uptr)p = (STRPTR)ext->si.sinfo.Buffer;
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
    case SLIDER_KIND: {
        IPTR p = Croi_GetTagData(tags, GTSL_Level, 0);
        if (p) {
            *(LONG *)(uptr)p = gt_pot_to_level(ext->si.pinfo.HorizPot, ext->sl_min, ext->sl_max);
            n++;
        }
        break;
    }
    case SCROLLER_KIND: {
        IPTR p = Croi_GetTagData(tags, GTSC_Top, 0);
        if (p) {
            LONG range = ext->sc_total - ext->sc_visible;
            *(LONG *)(uptr)p =
                (range > 0) ? (LONG)(((u32)ext->si.pinfo.HorizPot * (u32)range) / MAXPOT) : 0;
            n++;
        }
        break;
    }
    default:
        break;
    }
    return n;
}

// ---- IDCMP wrap (L8.4) ----------------------------------------------

// GT_GetIMsg(port): pop the next IntuiMessage from the window UserPort
// (the Leargas IDCMP ring), then do the gadtools-internal update for the
// referenced gadget — CYCLE/MX advance, CHECKBOX toggle — rewriting Code
// to the new state and re-rendering. Returns nullptr if no message.
struct IntuiMessage *Croi_GT_GetIMsg_Impl(struct MsgPort *port)
{
    if (!port) {
        return nullptr;
    }
    struct RingSlot slot;
    if (!Croi_GetMsg((struct CroiMsgPort *)port, &slot)) {
        return nullptr;
    }
    struct IntuiMessage *im = (struct IntuiMessage *)slot.payload;
    if (im && im->Class == IDCMP_GADGETUP && im->IAddress) {
        struct Gadget *g = (struct Gadget *)im->IAddress;
        // Only gadtools bool gadgets carry a GtGadgetExt on SpecialInfo
        // (string gadgets are GTYP_STRGADGET; the context is GTYP_GADGET0002).
        if (g->SpecialInfo && (g->GadgetType & GTYP_GTYPEMASK) == GTYP_BOOLGADGET) {
            struct GtGadgetExt *ext = (struct GtGadgetExt *)g->SpecialInfo;
            bool changed = false;
            switch (ext->kind) {
            case CYCLE_KIND:
            case MX_KIND:
                if (ext->nlabels) {
                    ext->active = (UWORD)((ext->active + 1) % ext->nlabels);
                    ext->label.IText = (UBYTE *)(uptr)ext->labels[ext->active];
                }
                im->Code = ext->active;
                changed = true;
                break;
            case CHECKBOX_KIND:
                g->Flags ^= (UWORD)GFLG_SELECTED;
                im->Code = (g->Flags & GFLG_SELECTED) ? 1 : 0;
                changed = true;
                break;
            default:
                break;
            }
            if (changed && im->IDCMPWindow) {
                Leargas_Gadget_Render(im->IDCMPWindow, g);
            }
        }
    }
    return im;
}

void Croi_GT_ReplyIMsg_Impl(struct IntuiMessage *imsg)
{
    if (imsg) {
        Leargas_IDCMP_DisposeMsg(imsg);
    }
}

void Croi_GT_RefreshWindow_Impl(struct Window *win, struct Requester *req)
{
    (void)req;
    if (win) {
        Leargas_Window_RenderGadgets(win);
    }
}

void Croi_GT_BeginRefresh_Impl(struct Window *win)
{
    (void)win; // no Layers / damage regions in v0
}

void Croi_GT_EndRefresh_Impl(struct Window *win, LONG complete)
{
    (void)complete;
    if (win) {
        Leargas_Window_RenderGadgets(win);
    }
}

// ---- Menu builder (L8.4) --------------------------------------------

// Allocate a MenuItem + its embedded IntuiText label as one shared-heap
// block (so FreeMenus does one free per item). `label` may be the
// NM_BARLABEL separator sentinel → a text-less, disabled item.
static struct MenuItem *gt_make_item(STRPTR label)
{
    u8 *blk = (u8 *)Croi_AllocShared(sizeof(struct MenuItem) + sizeof(struct IntuiText));
    if (!blk) {
        return nullptr;
    }
    struct MenuItem *mi = (struct MenuItem *)blk;
    struct IntuiText *it = (struct IntuiText *)(blk + sizeof(struct MenuItem));
    *mi = (struct MenuItem){ 0 };
    *it = (struct IntuiText){ 0 };
    bool isbar = (label == NM_BARLABEL);
    it->FrontPen = 0;
    it->BackPen = 1;
    it->LeftEdge = 2;
    it->TopEdge = 1;
    it->IText = isbar ? nullptr : (UBYTE *)(uptr)label;
    mi->ItemFill = it;
    mi->Flags = (UWORD)(ITEMTEXT | HIGHCOMP | (isbar ? 0 : ITEMENABLED));
    return mi;
}

// CreateMenusA(newmenu[], tags): walk the flat NewMenu array into an
// L5.3 Menu/MenuItem chain (shared heap). Returns the first Menu, or what
// was built so far on alloc failure (the caller FreeMenus to clean up).
struct Menu *Croi_GT_CreateMenusA_Impl(struct NewMenu *newmenu, struct TagItem *tags)
{
    (void)tags;
    if (!newmenu) {
        return nullptr;
    }
    struct Menu *first = nullptr, *curmenu = nullptr;
    struct MenuItem *curitem = nullptr, *cursub = nullptr;
    for (struct NewMenu *nm = newmenu; nm->nm_Type != NM_END; nm++) {
        switch (nm->nm_Type) {
        case NM_TITLE: {
            struct Menu *m = (struct Menu *)Croi_AllocShared(sizeof(struct Menu));
            if (!m) {
                return first;
            }
            *m = (struct Menu){ 0 };
            m->MenuName = (BYTE *)(uptr)nm->nm_Label;
            m->Flags = MENUENABLED;
            if (!first) {
                first = m;
            }
            if (curmenu) {
                curmenu->NextMenu = m;
            }
            curmenu = m;
            curitem = nullptr;
            cursub = nullptr;
            break;
        }
        case NM_ITEM: {
            if (!curmenu) {
                break;
            }
            struct MenuItem *mi = gt_make_item(nm->nm_Label);
            if (!mi) {
                return first;
            }
            if (!curmenu->FirstItem) {
                curmenu->FirstItem = mi;
            } else {
                curitem->NextItem = mi;
            }
            curitem = mi;
            cursub = nullptr;
            break;
        }
        case NM_SUB: {
            if (!curitem) {
                break;
            }
            struct MenuItem *mi = gt_make_item(nm->nm_Label);
            if (!mi) {
                return first;
            }
            if (!curitem->SubItem) {
                curitem->SubItem = mi;
            } else {
                cursub->NextItem = mi;
            }
            cursub = mi;
            break;
        }
        default:
            break;
        }
    }
    return first;
}

// LayoutMenusA(menu, vi, tags): assign bar/dropdown geometry over the
// VisualInfo's screen (the L5.3 layouter does the whole strip).
BOOL Croi_GT_LayoutMenusA_Impl(struct Menu *menu, APTR vi, struct TagItem *tags)
{
    (void)tags;
    struct CaraVisualInfo *cvi = (struct CaraVisualInfo *)vi;
    if (!menu || !cvi || !cvi->screen) {
        return FALSE;
    }
    Leargas_Menu_Layout(menu, cvi->screen);
    return TRUE;
}

// LayoutMenuItemsA: v0 — a single menu's items are laid out by
// LayoutMenusA (the whole-strip layouter), so this is a success no-op.
BOOL Croi_GT_LayoutMenuItemsA_Impl(struct MenuItem *firstitem, APTR vi, struct TagItem *tags)
{
    (void)firstitem;
    (void)vi;
    (void)tags;
    return TRUE;
}

// FreeMenus(menu): free the whole Menu/MenuItem/subitem chain. Each item
// block includes its IntuiText (one alloc), so one free per item.
void Croi_GT_FreeMenus_Impl(struct Menu *menu)
{
    struct Menu *m = menu;
    while (m) {
        struct Menu *nextm = m->NextMenu;
        struct MenuItem *it = m->FirstItem;
        while (it) {
            struct MenuItem *nexti = it->NextItem;
            struct MenuItem *su = it->SubItem;
            while (su) {
                struct MenuItem *nexts = su->NextItem;
                Croi_Free(su);
                su = nexts;
            }
            Croi_Free(it);
            it = nexti;
        }
        Croi_Free(m);
        m = nextm;
    }
}
