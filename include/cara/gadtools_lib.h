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

// gadtools-private per-gadget state, hung off Gadget.SpecialInfo for the
// L8.2 kinds (BUTTON/TEXT/NUMBER/CHECKBOX). Carries the label IntuiText
// (GadgetText points into it), the kind code, and a NUMBER_KIND value +
// its formatted text buffer. FreeGadgets frees this block. (STRING/
// INTEGER kinds, L8.3, need SpecialInfo == a StringInfo for the Leargas
// string-gadget renderer — handled separately when they land.)
struct GtGadgetExt {
    struct IntuiText label;
    UWORD kind;      // BUTTON_KIND / TEXT_KIND / NUMBER_KIND / CHECKBOX_KIND
    LONG number;     // NUMBER_KIND value
    char numbuf[16]; // formatted NUMBER text (label.IText points here)
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

// ---- Reserved-slot library hooks (`local` flavour) ------------------
// Vec slots 0..3. As with the other bases, OpenLibrary/CloseLibrary are
// served by exec's SYS_OpenLibrary path; these are trivial but the vec
// must point at real symbols (signatures match the generated vec table).
struct Library *Croi_GT_Open(struct Library *base, struct GadToolsBase *gtb);
void Croi_GT_Close(struct Library *base, struct GadToolsBase *gtb);
void Croi_GT_Expunge(struct Library *base, struct GadToolsBase *gtb);
ULONG Croi_GT_ExtFunc(struct Library *base, struct GadToolsBase *gtb);

#endif // CARA_GADTOOLS_LIB_H
