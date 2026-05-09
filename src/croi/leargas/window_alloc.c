// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LD — kernel-only heap path for OpenWindow / CloseWindow.
// Pulls in Croi_Alloc / Croi_Free which live in the kernel heap
// module; the dual-target window.c carries everything else.

#include <cara/alloc.h>
#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>

[[nodiscard]] struct Window *Leargas_OpenWindow(const struct NewWindow *nw)
{
    if (!nw) {
        return nullptr;
    }
    struct LeargasWindow *w = (struct LeargasWindow *)Croi_Alloc(sizeof(struct LeargasWindow));
    if (!w) {
        return nullptr;
    }
    if (Leargas_Window_InitInPlace(w, nw) != CARA_EOK) {
        Croi_Free(w);
        return nullptr;
    }
    Leargas_Window_LinkToScreen(w);
    Leargas_Window_Render(w);
    return &w->pub;
}

void Leargas_CloseWindow(struct Window *pub)
{
    if (!pub) {
        return;
    }
    struct LeargasWindow *w = Leargas_Window_FromPub(pub);

    // Phase 1 has no Layer support — blank the screen region the
    // window occupied with the screen's pen0 background colour so
    // closing leaves clean pixels behind. Phase 3 + Phase 4 redraw
    // the actual underneath via Layer tracking.
    struct LeargasScreen *ls = Leargas_Screen_FromPub(pub->WScreen);
    if (ls && ls->fb) {
        Dath_FillRect(ls->fb, pub->LeftEdge, pub->TopEdge, pub->Width, pub->Height, ls->pen0);
    }

    Leargas_Window_UnlinkFromScreen(w);
    Croi_Free(w);
}
