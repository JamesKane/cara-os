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

// LI — mouse-button + close-gadget delivery hooks (kernel installs the
// IDCMP posters; host builds leave them unset so the router unit test
// can install fakes / observe the pre-LI "no delivery" behaviour).
static Leargas_MouseButtonFn g_mousebtn_router = nullptr;
static Leargas_CloseWindowFn g_closewin_router = nullptr;

void Leargas_SetMouseButtonRouter(Leargas_MouseButtonFn fn)
{
    g_mousebtn_router = fn;
}

void Leargas_SetCloseWindowRouter(Leargas_CloseWindowFn fn)
{
    g_closewin_router = fn;
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
            // LH — an active string Inntin eats the keystroke (editing /
            // Return → GADGETUP). Only if it doesn't:
            if (Leargas_String_RouteKey(p, &ev)) {
                continue;
            }
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

        // Compute the post-event pointer position from the deltas and
        // clamp to the active screen, mirroring it into the public Screen
        // *before* any IDCMP message is posted below — so a button event
        // delivers the click's actual window-relative MouseX/Y (the
        // posters read screen->MouseX/Y via Leargas_BuildIntuiMessage).
        struct Screen *screen = Leargas_ActiveScreen();
        i32 nx = p->x + (i32)ev.ie_dx;
        i32 ny = p->y + (i32)ev.ie_dy;
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
            screen->MouseX = (WORD)nx;
            screen->MouseY = (WORD)ny;
        }

        // LE + LG + LI — left button. The down-stroke (no IECODE_UP_PREFIX)
        // re-focuses the window under the pointer and presses any gadget
        // there; the up-stroke releases the pressed gadget. Each redraw
        // happens under a hidden pointer so the cursor stays on top.
        if (ev.ie_code == IECODE_LBUTTON) {
            struct Window *hit = Leargas_Window_HitTest(screen, nx, ny);
            if (hit) {
                Leargas_Pointer_Hide(p);
                if (hit != Leargas_ActiveWindow()) {
                    Leargas_SetActiveWindow(hit); // LE — focus + title-bar redraw
                }
                i32 wx = nx - (i32)hit->LeftEdge;
                i32 wy = ny - (i32)hit->TopEdge;
                if (Leargas_Window_CloseHitTest(hit, wx, wy)) {
                    // LI — system close gadget: post IDCMP_CLOSEWINDOW.
                    // (Phase 1 acts on the down-stroke; V36+ waits for a
                    // release over the gadget.)
                    if (g_closewin_router) {
                        (void)g_closewin_router(hit);
                    }
                } else {
                    // LG — gadget hit-test in window-relative coordinates.
                    struct Gadget *g = Leargas_Gadget_HitTest(hit, wx, wy);
                    if (g) {
                        g->Flags |= GFLG_SELECTED;
                        Leargas_SetActiveGadget(g);
                        Leargas_Gadget_Render(hit, g);
                    } else if (g_mousebtn_router) {
                        // LI — a click not consumed by a gadget or the
                        // close box is delivered to the window so the
                        // client can hit-test its own surface (e.g. a
                        // drawer glyph). Phase 1 delivers the down-stroke
                        // (SELECTDOWN) only; SELECTUP/drag is deferred.
                        (void)g_mousebtn_router(hit, &ev);
                    }
                }
                Leargas_Pointer_Show(p);
            }
            // Fall through: a button event may also carry motion
            // deltas (typically zero — then Pointer_Move no-ops).
        } else if (ev.ie_code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
            // LG — release clears the pressed look on the gadget we
            // selected on the down-stroke. (Phase 1 assumes focus didn't
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
                // LJ — a boolean gadget with GACT_RELVERIFY posts
                // IDCMP_GADGETUP when released over itself (the V36+
                // button-release contract; a string Inntin instead posts
                // GADGETUP on Return, handled in string_gadget.c).
                if (aw && (g->GadgetType & GTYP_GTYPEMASK) == GTYP_BOOLGADGET &&
                    (g->Activation & GACT_RELVERIFY)) {
                    i32 wx = nx - (i32)aw->LeftEdge;
                    i32 wy = ny - (i32)aw->TopEdge;
                    if (Leargas_Gadget_HitTest(aw, wx, wy) == g) {
                        (void)Leargas_Gadget_RouteUp(aw, g);
                    }
                }
            }
        }

        Leargas_Pointer_Move(p, nx, ny);
    }
    return n;
}
