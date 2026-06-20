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

#include <cara/intuition_lib.h>
#include <cara/leargas.h>
#include <cara/log.h>
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
