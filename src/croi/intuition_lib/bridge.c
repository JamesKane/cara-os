// SPDX-License-Identifier: BSD-2-Clause
//
// intuition.library syscall-impl bodies — the kernel side of the five
// V36+ window/gadget LVOs Clar needs. Each is what
// src/croi/syscall/syscall.c routes the matching SYS_* arm to, and each
// bridges onto the Leargas_* window-system substrate (cara/leargas.h,
// docs/PHASE1_LEARGAS.md). The args arrive as SASOS pointers the user
// task allocated in the shared heap (NewWindow / Gadget / StringInfo),
// so the kernel dereferences them directly (SUM=1).
//
// Pointer validity: Phase 1 trusts the caller (single-user "god" model,
// PRINCIPLES §intro) — no bounds-checking of the shared-heap pointers
// beyond null guards.

#include <cara/dath.h>
#include <cara/graphics_lib.h>
#include <cara/intuition_lib.h>
#include <cara/leargas.h>
#include <cara/log.h>
#include <cara/time.h>
#include <cara/types.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>

// ---- Windows --------------------------------------------------------

struct Window *Croi_OpenWindow_Impl(struct NewWindow *nw)
{
    if (!nw) {
        return nullptr;
    }
    // Clar runs on the boot-opened Workbench screen: NewWindow.Screen
    // is nullptr, so Leargas opens onto its active screen.
    return Leargas_OpenWindow(nw);
}

void Croi_CloseWindow_Impl(struct Window *w)
{
    Leargas_CloseWindow(w);
}

// OpenWindowTagList(nw, tags) — build a NewWindow from WA_* tags over the
// optional template `nw`, then open it (L5.1, docs/LEARGAS_INTUITION.md
// §2.1). The taglist is walked kernel-side (utility's walkers are `local`
// U-mode RX-page code the kernel can't call); boolean WA_* fold into
// WFLG_* flags.
struct Window *Croi_OpenWindowTagList_Impl(struct NewWindow *nw_in, struct TagItem *tags)
{
    struct NewWindow nw = nw_in ? *nw_in : (struct NewWindow){ 0 };

    const struct TagItem *t = tags;
    while (t) {
        Tag id = t->ti_Tag;
        IPTR v = t->ti_Data;
        if (id == TAG_DONE) {
            break;
        }
        if (id == TAG_IGNORE) {
            t++;
            continue;
        }
        if (id == TAG_MORE) {
            t = (const struct TagItem *)(uptr)v;
            continue;
        }
        if (id == TAG_SKIP) {
            t += 1 + (i32)v;
            continue;
        }
        switch (id) {
        case WA_Left:
            nw.LeftEdge = (WORD)v;
            break;
        case WA_Top:
            nw.TopEdge = (WORD)v;
            break;
        case WA_Width:
            nw.Width = (WORD)v;
            break;
        case WA_Height:
            nw.Height = (WORD)v;
            break;
        case WA_DetailPen:
            nw.DetailPen = (UBYTE)v;
            break;
        case WA_BlockPen:
            nw.BlockPen = (UBYTE)v;
            break;
        case WA_IDCMP:
            nw.IDCMPFlags = (ULONG)v;
            break;
        case WA_Flags:
            nw.Flags = (ULONG)v;
            break;
        case WA_Gadgets:
            nw.FirstGadget = (struct Gadget *)(uptr)v;
            break;
        case WA_Checkmark:
            nw.CheckMark = (struct Image *)(uptr)v;
            break;
        case WA_Title:
            nw.Title = (UBYTE *)(uptr)v;
            break;
        case WA_CustomScreen:
            nw.Screen = (struct Screen *)(uptr)v;
            nw.Type = CUSTOMSCREEN;
            break;
        case WA_MinWidth:
            nw.MinWidth = (WORD)v;
            break;
        case WA_MinHeight:
            nw.MinHeight = (WORD)v;
            break;
        case WA_MaxWidth:
            nw.MaxWidth = (UWORD)v;
            break;
        case WA_MaxHeight:
            nw.MaxHeight = (UWORD)v;
            break;
        case WA_SizeGadget:
            if (v) {
                nw.Flags |= WFLG_SIZEGADGET;
            }
            break;
        case WA_DragBar:
            if (v) {
                nw.Flags |= WFLG_DRAGBAR;
            }
            break;
        case WA_DepthGadget:
            if (v) {
                nw.Flags |= WFLG_DEPTHGADGET;
            }
            break;
        case WA_CloseGadget:
            if (v) {
                nw.Flags |= WFLG_CLOSEGADGET;
            }
            break;
        case WA_Backdrop:
            if (v) {
                nw.Flags |= WFLG_BACKDROP;
            }
            break;
        case WA_Borderless:
            if (v) {
                nw.Flags |= WFLG_BORDERLESS;
            }
            break;
        case WA_Activate:
            if (v) {
                nw.Flags |= WFLG_ACTIVATE;
            }
            break;
        case WA_GimmeZeroZero:
            if (v) {
                nw.Flags |= WFLG_GIMMEZEROZERO;
            }
            break;
        default:
            break; // unrecognised WA_* tag — ignored (v0)
        }
        t++;
    }
    return Leargas_OpenWindow(&nw);
}

// ModifyIDCMP(window, flags) — set the window's IDCMP class mask. v0
// updates the flag field; the UserPort is created at OpenWindow when IDCMP
// was requested (a window opened with no IDCMP that later enables it does
// not gain a port in v0 — see docs/LEARGAS_INTUITION.md §2.4 gaps).
void Croi_ModifyIDCMP_Impl(struct Window *w, ULONG idcmpFlags)
{
    if (w) {
        w->IDCMPFlags = idcmpFlags;
    }
}

// ---- Window ops + activation (L5.2) ---------------------------------

void Croi_MoveWindow_Impl(struct Window *w, WORD dx, WORD dy)
{
    Leargas_MoveWindow(w, dx, dy);
}

void Croi_SizeWindow_Impl(struct Window *w, WORD dx, WORD dy)
{
    Leargas_SizeWindow(w, dx, dy);
}

void Croi_WindowToFront_Impl(struct Window *w)
{
    Leargas_WindowToFront(w);
}

void Croi_WindowToBack_Impl(struct Window *w)
{
    Leargas_WindowToBack(w);
}

void Croi_SetWindowTitles_Impl(struct Window *w, STRPTR windowTitle, STRPTR screenTitle)
{
    Leargas_SetWindowTitles(w, (const char *)windowTitle, (const char *)screenTitle);
}

// ActivateWindow — make `w` the focused window and post the IDCMP
// active/inactive edges (gated on each window's IDCMPFlags + UserPort).
void Croi_ActivateWindow_Impl(struct Window *w)
{
    struct Window *old = Leargas_ActiveWindow();
    if (old == w) {
        return;
    }
    Leargas_SetActiveWindow(w);
    if (old) {
        (void)Leargas_IDCMP_PostClass(old, IDCMP_INACTIVEWINDOW);
    }
    if (w) {
        (void)Leargas_IDCMP_PostClass(w, IDCMP_ACTIVEWINDOW);
    }
}

// ---- Menus (L5.3) ---------------------------------------------------

void Croi_SetMenuStrip_Impl(struct Window *w, struct Menu *menu)
{
    Leargas_SetMenuStrip(w, menu);
}

void Croi_ClearMenuStrip_Impl(struct Window *w)
{
    Leargas_ClearMenuStrip(w);
}

struct MenuItem *Croi_ItemAddress_Impl(struct Menu *menuStrip, UWORD menuNumber)
{
    return Leargas_ItemAddress(menuStrip, menuNumber);
}

// ---- Rendering helpers (L5.5) ---------------------------------------

static u32 itext_strlen(const UBYTE *s)
{
    u32 n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

// IntuiTextLength — pixel width of the text (monospace v0: len * font W).
LONG Croi_IntuiTextLength_Impl(struct IntuiText *iText)
{
    if (!iText || !iText->IText) {
        return 0;
    }
    return (LONG)(itext_strlen(iText->IText) * dath_font_8x8.width);
}

// PrintIText — render an IntuiText chain into the RastPort at (x,y) over
// the graphics.library primitives (each segment carries its own pens).
void Croi_PrintIText_Impl(struct RastPort *rp, struct IntuiText *iText, WORD x, WORD y)
{
    if (!rp) {
        return;
    }
    for (struct IntuiText *t = iText; t; t = t->NextText) {
        if (!t->IText) {
            continue;
        }
        Croi_Gfx_SetAPen_Impl(rp, t->FrontPen);
        Croi_Gfx_SetBPen_Impl(rp, t->BackPen);
        Croi_Gfx_SetDrMd_Impl(rp, t->DrawMode);
        Croi_Gfx_Move_Impl(rp, (WORD)(x + t->LeftEdge), (WORD)(y + t->TopEdge));
        Croi_Gfx_Text_Impl(rp, (STRPTR)t->IText, itext_strlen(t->IText));
    }
}

// DrawBorder — render a Border polyline chain into the RastPort at (x,y).
void Croi_DrawBorder_Impl(struct RastPort *rp, struct Border *border, WORD x, WORD y)
{
    if (!rp) {
        return;
    }
    for (struct Border *b = border; b; b = b->NextBorder) {
        if (!b->XY || b->Count <= 0) {
            continue;
        }
        Croi_Gfx_SetAPen_Impl(rp, b->FrontPen);
        Croi_Gfx_SetDrMd_Impl(rp, b->DrawMode);
        WORD bx = (WORD)(x + b->LeftEdge);
        WORD by = (WORD)(y + b->TopEdge);
        Croi_Gfx_Move_Impl(rp, (WORD)(bx + b->XY[0]), (WORD)(by + b->XY[1]));
        for (i32 i = 1; i < b->Count; i++) {
            Croi_Gfx_Draw_Impl(rp, (WORD)(bx + b->XY[i * 2]), (WORD)(by + b->XY[i * 2 + 1]));
        }
    }
}

// ---- Gadget-list verbs (L5.5) ---------------------------------------

UWORD Croi_AddGList_Impl(struct Window *w, struct Gadget *gadget, ULONG position, LONG numGad,
                         struct Requester *requester)
{
    (void)position;
    (void)requester;
    UWORD added = 0;
    struct Gadget *g = gadget;
    while (g && numGad != 0) {
        struct Gadget *next = g->NextGadget;
        Leargas_AddGadget(w, g);
        Leargas_Gadget_Render(w, g);
        added++;
        if (numGad > 0) {
            numGad--;
        }
        g = next;
    }
    return added;
}

UWORD Croi_RemoveGList_Impl(struct Window *remPtr, struct Gadget *gadget, LONG numGad)
{
    UWORD removed = 0;
    struct Gadget *g = gadget;
    while (g && numGad != 0) {
        struct Gadget *next = g->NextGadget;
        Leargas_RemoveGadget(remPtr, g);
        removed++;
        if (numGad > 0) {
            numGad--;
        }
        g = next;
    }
    return removed;
}

void Croi_OnGadget_Impl(struct Gadget *gadget, struct Window *window, struct Requester *requester)
{
    (void)requester;
    if (!gadget) {
        return;
    }
    gadget->Flags &= ~(UWORD)GFLG_DISABLED;
    Leargas_Gadget_Render(window, gadget);
}

void Croi_OffGadget_Impl(struct Gadget *gadget, struct Window *window, struct Requester *requester)
{
    (void)requester;
    if (!gadget) {
        return;
    }
    gadget->Flags |= (UWORD)GFLG_DISABLED;
    Leargas_Gadget_Render(window, gadget);
}

void Croi_RefreshGList_Impl(struct Gadget *gadgets, struct Window *window,
                            struct Requester *requester, LONG numGad)
{
    (void)requester;
    struct Gadget *g = gadgets;
    while (g && numGad != 0) {
        Leargas_Gadget_Render(window, g);
        if (numGad > 0) {
            numGad--;
        }
        g = g->NextGadget;
    }
}

void Croi_RefreshWindowFrame_Impl(struct Window *window)
{
    if (!window) {
        return;
    }
    struct LeargasWindow *lw = Leargas_Window_FromPub(window);
    if (lw) {
        Leargas_Window_Render(lw);
    }
}

// ---- Feedback / timing (L5.4) ---------------------------------------

// CurrentTime — fill *seconds/*micros from the monotonic S-mode clock.
void Croi_CurrentTime_Impl(ULONG *seconds, ULONG *micros)
{
    u64 ns = Croi_Time_Now();
    if (seconds) {
        *seconds = (ULONG)(ns / 1000000000ull);
    }
    if (micros) {
        *micros = (ULONG)((ns % 1000000000ull) / 1000ull);
    }
}

// DoubleClick — TRUE if the click time follows the start within the
// double-click interval (v0: a fixed 500 ms; AmigaOS reads Preferences).
BOOL Croi_DoubleClick_Impl(ULONG sSeconds, ULONG sMicros, ULONG cSeconds, ULONG cMicros)
{
    i64 total = ((i64)cSeconds - (i64)sSeconds) * 1000000 + ((i64)cMicros - (i64)sMicros);
    return (total >= 0 && total < 500000) ? TRUE : FALSE;
}

// DisplayBeep — v0: log it (no audio device / screen-flash timing yet).
void Croi_DisplayBeep_Impl(struct Screen *screen)
{
    (void)screen;
    LOG_INFO("intu", "DisplayBeep");
}

// ReportMouse — enable/disable IDCMP_MOUSEMOVE delivery for a window.
void Croi_ReportMouse_Impl(LONG flag, struct Window *window)
{
    if (!window) {
        return;
    }
    if (flag) {
        window->Flags |= (ULONG)WFLG_REPORTMOUSE;
        window->IDCMPFlags |= IDCMP_MOUSEMOVE;
    } else {
        window->Flags &= ~(ULONG)WFLG_REPORTMOUSE;
        window->IDCMPFlags &= ~(ULONG)IDCMP_MOUSEMOVE;
    }
}

// ---- Gadgets --------------------------------------------------------

UWORD Croi_AddGadget_Impl(struct Window *w, struct Gadget *g, ULONG position)
{
    // Phase 1 always appends; `position` is honored later (V36+
    // semantics: ~0 = append, otherwise insert at that ordinal).
    (void)position;
    Leargas_AddGadget(w, g);
    Leargas_Gadget_Render(w, g);
    return 0;
}

UWORD Croi_RemoveGadget_Impl(struct Window *w, struct Gadget *g)
{
    Leargas_RemoveGadget(w, g);
    return 0;
}

BOOL Croi_ActivateGadget_Impl(struct Gadget *g, struct Window *w, struct Requester *req)
{
    (void)req; // Phase 1 has no requesters
    if (!g) {
        return FALSE;
    }
    Leargas_SetActiveGadget(g);
    Leargas_Gadget_Render(w, g);
    return TRUE;
}
