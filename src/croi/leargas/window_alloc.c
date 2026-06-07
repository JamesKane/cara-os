// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LD — kernel-only heap path for OpenWindow / CloseWindow.
// Pulls in Croi_Alloc / Croi_Free which live in the kernel heap
// module; the dual-target window.c carries everything else.

#include <cara/alloc.h>
#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/msgport.h>
#include <cara/sched.h>
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

    // LF — give the window an IDCMP UserPort if it requested events and
    // there's a task to own it. Croi_AllocSignal returns -1 when there
    // is no current task (the pre-Sched_Init boot demo), so the window
    // simply opens portless there; the kernel test (post Sched_Init)
    // gets a real port. Best-effort: a failure leaves UserPort null and
    // the window still opens.
    if (nw->IDCMPFlags != 0) {
        i32 sig = Croi_AllocSignal();
        if (sig >= 0) {
            struct CroiMsgPort *port = Croi_CreateMsgPort(Sched_Current(), (u32)sig,
                                                          LEARGAS_IDCMP_RING_CAP);
            if (port) {
                w->pub.UserPort = (struct MsgPort *)port;
                w->idcmp_sigbit = sig;
            } else {
                Croi_FreeSignal(sig);
            }
        }
    }

    Leargas_Window_LinkToScreen(w);
    Leargas_Window_Render(w);

    // V36+: a window opened with WFLG_ACTIVATE becomes the focused
    // window. SetActiveWindow flips WFLG_WINDOWACTIVE + redraws it in
    // active chrome (and deactivates whatever was focused before).
    if (w->pub.Flags & WFLG_ACTIVATE) {
        Leargas_SetActiveWindow(&w->pub);
    }
    return &w->pub;
}

void Leargas_CloseWindow(struct Window *pub)
{
    if (!pub) {
        return;
    }
    struct LeargasWindow *w = Leargas_Window_FromPub(pub);

    // LF — tear down the IDCMP UserPort. Drain and free any messages the
    // client never consumed first (they're heap-allocated IntuiMessages),
    // then destroy the port and release its signal bit. FreeSignal acts
    // on the current task, so Phase 1 assumes the closer is the opener —
    // true for the boot task and the kernel test.
    if (pub->UserPort) {
        struct IntuiMessage *im;
        while (Leargas_IDCMP_GetMsg(pub, &im)) {
            Leargas_IDCMP_DisposeMsg(im);
        }
        Croi_DestroyMsgPort((struct CroiMsgPort *)pub->UserPort);
        pub->UserPort = nullptr;
    }
    if (w->idcmp_sigbit >= 0) {
        Croi_FreeSignal(w->idcmp_sigbit);
        w->idcmp_sigbit = -1;
    }

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
