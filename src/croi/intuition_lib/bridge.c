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
