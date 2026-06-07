// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LC + LE + LF — input router. Drains the L0 ring and bridges
// each event to its handler: RAWMOUSE motion drives the pointer (LC),
// a left-button-down re-focuses the window under the pointer (LE), and
// a RAWKEY is routed to the focused window's IDCMP UserPort via the
// installed key hook (LF). Phase 1 polls; Phase 3 moves the producer
// to a U-mode HID Gleas + a blocking ring read but the contract here
// stays the same.

#include <cara/leargas.h>
#include <cara/types.h>
#include <devices/inputevent.h>

// LF — key-routing hook. Kept in this dual-target file (a plain
// function pointer) so the router carries no kernel dependency; the
// kernel installs Leargas_IDCMP_RouteKey at boot. Unset (host builds,
// the router unit test) → RAWKEY events are counted and dropped, the
// pre-LF behaviour.
static Leargas_KeyRouteFn g_key_router = nullptr;

void Leargas_SetKeyRouter(Leargas_KeyRouteFn fn)
{
    g_key_router = fn;
}

static i32 clamp_i32(i32 v, i32 lo, i32 hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

u32 Leargas_Input_Drain(struct LeargasPointer *p)
{
    if (!p) {
        return 0;
    }

    u32 n = 0;
    struct LeargasInputEvent ev;
    while (Leargas_Input_Read(&ev)) {
        n++;
        if (ev.ie_class == IECLASS_RAWKEY) {
            // LF — hand the keystroke to the focused window's IDCMP
            // port via the installed hook (kernel: Leargas_IDCMP_RouteKey).
            // No hook (host builds) or no focused window → dropped.
            if (g_key_router) {
                (void)g_key_router(Leargas_ActiveWindow(), &ev);
            }
            continue;
        }
        if (ev.ie_class != IECLASS_RAWMOUSE) {
            // Other classes have no Phase 1 consumer yet; Phase 1 has a
            // single consumer of the ring, so they're dropped here.
            continue;
        }

        // LE + LG — left button. The down-stroke (no IECODE_UP_PREFIX)
        // re-focuses the window under the pointer and presses any gadget
        // there; the up-stroke releases the pressed gadget. Each redraw
        // happens under a hidden pointer so the cursor stays on top.
        if (ev.ie_code == IECODE_LBUTTON) {
            struct Screen *screen = Leargas_ActiveScreen();
            struct Window *hit = Leargas_Window_HitTest(screen, p->x, p->y);
            if (hit) {
                Leargas_Pointer_Hide(p);
                if (hit != Leargas_ActiveWindow()) {
                    Leargas_SetActiveWindow(hit); // LE — focus + title-bar redraw
                }
                // LG — gadget hit-test in window-relative coordinates.
                struct Gadget *g = Leargas_Gadget_HitTest(hit, p->x - (i32)hit->LeftEdge,
                                                          p->y - (i32)hit->TopEdge);
                if (g) {
                    g->Flags |= GFLG_SELECTED;
                    Leargas_SetActiveGadget(g);
                    Leargas_Gadget_Render(hit, g);
                }
                Leargas_Pointer_Show(p);
            }
            // Fall through: a button event may also carry motion
            // deltas (typically zero — then Pointer_Move no-ops).
        } else if (ev.ie_code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
            // LG — release clears the pressed look on the gadget we
            // selected on the down-stroke. (IDCMP_GADGETUP for
            // GACT_RELVERIFY gadgets is LH; Phase 1 assumes focus didn't
            // change between down and up, so the gadget's window is the
            // active one.)
            struct Gadget *g = Leargas_ActiveGadget();
            if (g && (g->Flags & GFLG_SELECTED)) {
                struct Window *aw = Leargas_ActiveWindow();
                Leargas_Pointer_Hide(p);
                g->Flags &= ~(UWORD)GFLG_SELECTED;
                if (aw) {
                    Leargas_Gadget_Render(aw, g);
                }
                Leargas_Pointer_Show(p);
            }
        }

        i32 nx = p->x + (i32)ev.ie_dx;
        i32 ny = p->y + (i32)ev.ie_dy;

        struct Screen *screen = Leargas_ActiveScreen();
        if (screen) {
            i32 max_x = (i32)(u16)screen->Width - 1;
            i32 max_y = (i32)(u16)screen->Height - 1;
            if (max_x < 0) {
                max_x = 0;
            }
            if (max_y < 0) {
                max_y = 0;
            }
            nx = clamp_i32(nx, 0, max_x);
            ny = clamp_i32(ny, 0, max_y);
            // Reflect the (possibly-clamped) position back into the
            // V36+ public Screen for any consumer that reads it
            // (Clar's hit-testing in LE-onwards will).
            screen->MouseX = (WORD)nx;
            screen->MouseY = (WORD)ny;
        }

        Leargas_Pointer_Move(p, nx, ny);
    }
    return n;
}
