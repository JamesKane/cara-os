// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ gadtools.library LVOs Croi
// serves (docs/LEARGAS_GADTOOLS.md). gadtools is `syscall` flavour: each
// LVO reaches src/croi/syscall/syscall.c via its trampoline
// (Cara_Trampoline_GT_<Name> in src/croi/gadtools/trampolines.S), and the
// dispatcher routes the matching SYS_GT_* to the Croi_GT_*_Impl below.
// The bodies bridge onto the Leargas gadget substrate (cara/leargas.h)
// and graphics.library (cara/graphics_lib.h) — gadtools builds plain
// struct Gadgets, NOT BOOPSI objects (docs/LEARGAS_GADTOOLS.md §2.1).

#ifndef CARA_GADTOOLS_LIB_H
#define CARA_GADTOOLS_LIB_H

#include <cara/types.h>
#include <exec/types.h>
#include <intuition/intuition.h> // struct IntuiText
#include <intuition/screens.h>   // struct DrawInfo

struct Screen;
struct Gadget;
struct Window;
struct Requester;
struct NewGadget;
struct TagItem;
struct Library;
struct GadToolsBase;

// gadtools-private per-gadget state, hung off Gadget.SpecialInfo for all
// kinds. The StringInfo is FIRST (offset 0): for STRING/INTEGER kinds the
// gadget is GTYP_STRGADGET and the Leargas string renderer reads
// g->SpecialInfo as a struct StringInfo* — which is &ext->sinfo (offset
// 0). gadtools casts the same pointer to GtGadgetExt*. So one allocation
// serves both views, and FreeGadgets does one free. For non-string kinds
// the sinfo is unused/zeroed (Leargas never reads it).
#define CARA_GT_STRBUF 64

struct GtGadgetExt {
    struct StringInfo sinfo;      // offset 0 — STRING/INTEGER (Leargas reads it)
    struct IntuiText label;       // GadgetText points here
    UWORD kind;                   // *_KIND
    LONG number;                  // NUMBER value / INTEGER parsed result
    char numbuf[16];              // formatted NUMBER text
    const char **labels;          // CYCLE/MX label array (app-owned, NULL-term)
    UWORD active;                 // CYCLE/MX active index
    UWORD nlabels;                // CYCLE/MX label count
    char strbuf[CARA_GT_STRBUF];  // STRING/INTEGER edit buffer
    char undobuf[CARA_GT_STRBUF]; // StringInfo undo scratch
};

// VisualInfo (L8.1): the opaque GetVisualInfoA handle. Allocated on the
// SASOS shared heap so it is valid in U-mode; carries the screen and an
// inline DrawInfo (its dri_Pens points at the embedded pen array). The
// app treats it as an opaque APTR; GetScreenDrawInfo (later) can hand out
// &cvi->dri.
struct CaraVisualInfo {
    struct DrawInfo dri;
    UWORD pens[NUMDRIPENS];
    struct Screen *screen;
};

// ---- Library + render context (L8.1) --------------------------------
APTR Croi_GT_GetVisualInfoA_Impl(struct Screen *screen, struct TagItem *tags);
void Croi_GT_FreeVisualInfo_Impl(APTR vi);
struct Gadget *Croi_GT_CreateContext_Impl(struct Gadget **glistptr);
struct Gadget *Croi_GT_FreeGadgets_Impl(struct Gadget *glist);

// ---- Gadget factory + easy kinds (L8.2) -----------------------------
struct Gadget *Croi_GT_CreateGadgetA_Impl(ULONG kind, struct Gadget *prevGad, struct NewGadget *ng,
                                          struct TagItem *tags);
void Croi_GT_SetGadgetAttrsA_Impl(struct Gadget *gad, struct Window *win, struct Requester *req,
                                  struct TagItem *tags);

// ---- Bevel + choice/edit kinds + attribute read (L8.3) --------------
struct RastPort;
void Croi_GT_DrawBevelBoxA_Impl(struct RastPort *rp, WORD left, WORD top, WORD width, WORD height,
                                struct TagItem *tags);
ULONG Croi_GT_GetGadgetAttrsA_Impl(struct Gadget *gad, struct Window *win, struct Requester *req,
                                   struct TagItem *tags);

// ---- IDCMP wrap + menu builder (L8.4) -------------------------------
struct MsgPort;
struct IntuiMessage;
struct Menu;
struct MenuItem;
struct NewMenu;
// GT_GetIMsg pops the next IntuiMessage from the window's UserPort and
// performs the gadtools-internal update for the referenced gadget (CYCLE
// advance / CHECKBOX toggle / MX select). GT_ReplyIMsg disposes it.
struct IntuiMessage *Croi_GT_GetIMsg_Impl(struct MsgPort *port);
void Croi_GT_ReplyIMsg_Impl(struct IntuiMessage *imsg);
void Croi_GT_RefreshWindow_Impl(struct Window *win, struct Requester *req);
void Croi_GT_BeginRefresh_Impl(struct Window *win);
void Croi_GT_EndRefresh_Impl(struct Window *win, LONG complete);
// Menu builder: NewMenu[] → L5.3 Menu/MenuItem chain; LayoutMenusA
// assigns geometry (Leargas_Menu_Layout); FreeMenus frees.
struct Menu *Croi_GT_CreateMenusA_Impl(struct NewMenu *newmenu, struct TagItem *tags);
BOOL Croi_GT_LayoutMenusA_Impl(struct Menu *menu, APTR vi, struct TagItem *tags);
BOOL Croi_GT_LayoutMenuItemsA_Impl(struct MenuItem *firstitem, APTR vi, struct TagItem *tags);
void Croi_GT_FreeMenus_Impl(struct Menu *menu);

// ---- Reserved-slot library hooks (`local` flavour) ------------------
// Vec slots 0..3. As with the other bases, OpenLibrary/CloseLibrary are
// served by exec's SYS_OpenLibrary path; these are trivial but the vec
// must point at real symbols (signatures match the generated vec table).
struct Library *Croi_GT_Open(struct Library *base, struct GadToolsBase *gtb);
void Croi_GT_Close(struct Library *base, struct GadToolsBase *gtb);
void Croi_GT_Expunge(struct Library *base, struct GadToolsBase *gtb);
ULONG Croi_GT_ExtFunc(struct Library *base, struct GadToolsBase *gtb);

#endif // CARA_GADTOOLS_LIB_H
