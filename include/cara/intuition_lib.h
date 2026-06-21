// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ intuition.library LVOs that
// Croi serves. The five window/gadget LVOs are `syscall` flavour: each
// reaches src/croi/syscall/syscall.c via its trampoline
// (Cara_Trampoline_<Name> in src/croi/intuition_lib/trampolines.S),
// and the dispatcher routes the matching SYS_* to the Croi_*_Impl
// below. As with exec.library, the impl signature is the V36+ canonical
// signature minus the trailing `struct IntuitionBase *base` (CaraOS's
// syscall path doesn't need the base — the kernel reaches the Leargas
// substrate directly).
//
// Each body bridges onto the Leargas_* window system (cara/leargas.h):
//   OpenWindow      -> Leargas_OpenWindow
//   CloseWindow     -> Leargas_CloseWindow
//   AddGadget       -> Leargas_AddGadget (+ render)
//   RemoveGadget    -> Leargas_RemoveGadget
//   ActivateGadget  -> Leargas_SetActiveGadget (+ render)

#ifndef CARA_INTUITION_LIB_H
#define CARA_INTUITION_LIB_H

#include <cara/types.h>
#include <exec/types.h>

struct Window;
struct Gadget;
struct NewWindow;
struct Requester;
struct Library;
struct IntuitionBase;

// ---- Windows --------------------------------------------------------

struct Window *Croi_OpenWindow_Impl(struct NewWindow *nw);
void Croi_CloseWindow_Impl(struct Window *w);

// ---- Tag opener + IDCMP (L5.1) --------------------------------------
struct TagItem;
// OpenWindowTagList(nw, tags): build a NewWindow from WA_* tags over the
// optional template nw, then Leargas_OpenWindow. nw may be nullptr.
struct Window *Croi_OpenWindowTagList_Impl(struct NewWindow *nw, struct TagItem *tags);
void Croi_ModifyIDCMP_Impl(struct Window *w, ULONG idcmpFlags);

// ---- Window ops + activation (L5.2) ---------------------------------
void Croi_MoveWindow_Impl(struct Window *w, WORD dx, WORD dy);
void Croi_SizeWindow_Impl(struct Window *w, WORD dx, WORD dy);
void Croi_WindowToFront_Impl(struct Window *w);
void Croi_WindowToBack_Impl(struct Window *w);
void Croi_SetWindowTitles_Impl(struct Window *w, STRPTR windowTitle, STRPTR screenTitle);
void Croi_ActivateWindow_Impl(struct Window *w);

// ---- Menus (L5.3) ---------------------------------------------------
struct Menu;
struct MenuItem;
void Croi_SetMenuStrip_Impl(struct Window *w, struct Menu *menu);
void Croi_ClearMenuStrip_Impl(struct Window *w);
struct MenuItem *Croi_ItemAddress_Impl(struct Menu *menuStrip, UWORD menuNumber);

// ---- Feedback / timing (L5.4) ---------------------------------------
struct Screen;
void Croi_CurrentTime_Impl(ULONG *seconds, ULONG *micros);
BOOL Croi_DoubleClick_Impl(ULONG sSeconds, ULONG sMicros, ULONG cSeconds, ULONG cMicros);
void Croi_DisplayBeep_Impl(struct Screen *screen);
void Croi_ReportMouse_Impl(LONG flag, struct Window *window);

// ---- Rendering helpers + gadget widen (L5.5) ------------------------
struct RastPort;
struct IntuiText;
struct Border;
LONG Croi_IntuiTextLength_Impl(struct IntuiText *iText);
void Croi_PrintIText_Impl(struct RastPort *rp, struct IntuiText *iText, WORD x, WORD y);
void Croi_DrawBorder_Impl(struct RastPort *rp, struct Border *border, WORD x, WORD y);
UWORD Croi_AddGList_Impl(struct Window *w, struct Gadget *gadget, ULONG position, LONG numGad,
                         struct Requester *requester);
UWORD Croi_RemoveGList_Impl(struct Window *remPtr, struct Gadget *gadget, LONG numGad);
void Croi_OnGadget_Impl(struct Gadget *gadget, struct Window *window, struct Requester *requester);
void Croi_OffGadget_Impl(struct Gadget *gadget, struct Window *window, struct Requester *requester);
void Croi_RefreshGList_Impl(struct Gadget *gadgets, struct Window *window,
                            struct Requester *requester, LONG numGad);
void Croi_RefreshWindowFrame_Impl(struct Window *window);

// ---- Gadgets --------------------------------------------------------

UWORD Croi_AddGadget_Impl(struct Window *w, struct Gadget *g, ULONG position);
UWORD Croi_RemoveGadget_Impl(struct Window *w, struct Gadget *g);
BOOL Croi_ActivateGadget_Impl(struct Gadget *g, struct Window *w, struct Requester *req);

// ---- Reserved-slot library hooks (`local` flavour) ------------------
// Vec slots 0..3. As with exec.library, OpenLibrary/CloseLibrary are
// served by exec's SYS_OpenLibrary path, so these are never invoked via
// the user-side stub in practice — but the vec table must point at real
// symbols. Signatures match the generated intuition_vec.c (V36+ args +
// trailing struct IntuitionBase *).

struct Library *Croi_Intuition_Open(struct Library *base, struct IntuitionBase *ib);
void Croi_Intuition_Close(struct Library *base, struct IntuitionBase *ib);
void Croi_Intuition_Expunge(struct Library *base, struct IntuitionBase *ib);
ULONG Croi_Intuition_ExtFunc(struct Library *base, struct IntuitionBase *ib);

#endif // CARA_INTUITION_LIB_H
