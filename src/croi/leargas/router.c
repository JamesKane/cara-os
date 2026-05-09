// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LC — mouse-motion router. Bridges L0 (input ring) to LA
// (pointer rendering): accumulates IECLASS_RAWMOUSE deltas onto the
// pointer's position, clamps against the active screen extent, calls
// Pointer_Move. Phase 1 polls; Phase 3 moves the producer to a
// U-mode HID Gleas + a blocking ring read but the contract here
// stays the same.

#include <cara/leargas.h>
#include <cara/types.h>
#include <devices/inputevent.h>

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
        if (ev.ie_class != IECLASS_RAWMOUSE) {
            // RAWKEY / etc. — LF will route keyboard events to the
            // focused window once it lands. Phase 1 has a single
            // consumer of the ring, so anything we don't handle is
            // dropped on the floor here.
            continue;
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
