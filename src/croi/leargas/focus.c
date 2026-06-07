// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LE — window focus + activation. A single process-wide
// active-window pointer; a mouse button-down (routed from router.c)
// hit-tests the active screen's window list to pick the new focus.
// WFLG_WINDOWACTIVE mirrors the focus state and the title bar redraws
// active vs. inactive (window.c Render reads the flag).
//
// Dual-target: pure pointer bookkeeping plus a call into the
// (also dual-target) window renderer, so host unit tests exercise the
// whole path. No kernel-only dependencies.
//
// IDCMP_ACTIVEWINDOW / IDCMP_INACTIVEWINDOW *delivery* belongs to LF
// (it owns the per-window UserPort + IntuiMessage). SetActiveWindow
// is the single chokepoint LF extends to post those messages — see
// the marked seam below.

#include <cara/leargas.h>
#include <cara/types.h>

// Phase 1: exactly one focused window, process-wide. Phase 3 evolves
// to per-screen focus once multiple screens exist.
static struct Window *g_active_window;

[[nodiscard]] struct Window *Leargas_ActiveWindow(void)
{
    return g_active_window;
}

void Leargas_Focus_Reset(void)
{
    g_active_window = nullptr;
}

[[nodiscard]] struct Window *Leargas_Window_HitTest(struct Screen *screen, i32 x, i32 y)
{
    if (!screen) {
        return nullptr;
    }
    // FirstWindow is the front-most (LinkToScreen head-inserts), so a
    // forward walk visits windows top-to-bottom; the first containing
    // window is the topmost hit.
    for (struct Window *w = screen->FirstWindow; w; w = w->NextWindow) {
        i32 x0 = w->LeftEdge;
        i32 y0 = w->TopEdge;
        i32 x1 = x0 + w->Width;  // right edge, exclusive
        i32 y1 = y0 + w->Height; // bottom edge, exclusive
        if (x >= x0 && x < x1 && y >= y0 && y < y1) {
            return w;
        }
    }
    return nullptr;
}

void Leargas_SetActiveWindow(struct Window *w)
{
    if (w == g_active_window) {
        return; // idempotent — no flag churn, no redraw
    }

    struct Window *old = g_active_window;
    g_active_window = w;

    if (old) {
        old->Flags &= ~(ULONG)WFLG_WINDOWACTIVE;
        struct LeargasWindow *lo = Leargas_Window_FromPub(old);
        if (lo) {
            Leargas_Window_Render(lo);
        }
    }
    if (w) {
        w->Flags |= (ULONG)WFLG_WINDOWACTIVE;
        struct LeargasWindow *ln = Leargas_Window_FromPub(w);
        if (ln) {
            Leargas_Window_Render(ln);
        }
    }

    // LF seam: post IDCMP_INACTIVEWINDOW to `old` and
    // IDCMP_ACTIVEWINDOW to `w` (each gated on the window's
    // IDCMPFlags + a non-null UserPort) once the per-window IDCMP
    // MsgPort + IntuiMessage path lands.
}
