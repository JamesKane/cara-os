// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LI — the pure dual-target pieces
// of window mouse-button + close-gadget delivery:
//   - Leargas_Window_CloseHitTest geometry and flag-gating.
//   - Leargas_BuildIntuiMessage class mapping (RAWMOUSE button ->
//     IDCMP_MOUSEBUTTONS, RAWMOUSE motion -> IDCMP_MOUSEMOVE, RAWKEY ->
//     IDCMP_RAWKEY) and Code pass-through.
// The kernel test (idcmp_buttons) covers the posters + router end-to-end
// over the real shared-heap MsgPort; this keeps the fast CI coverage.

#include <cara/leargas.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <stdio.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_button: FAIL: %s\n", msg);
    return code;
}

int main(void)
{
    // ---- CloseHitTest ------------------------------------------------------
    struct Window w = { 0 };
    w.Width = 120;
    w.Height = 70;
    w.BorderTop = LEARGAS_WINDOW_DEFAULT_BORDER_TOP; // 11
    w.Flags = WFLG_DRAGBAR | WFLG_CLOSEGADGET;

    // Close box: wx in [Width-BorderTop, Width) = [109,120), wy in [0,11).
    if (!Leargas_Window_CloseHitTest(&w, 112, 5)) {
        return fail("expected close hit inside the X box", 1);
    }
    if (!Leargas_Window_CloseHitTest(&w, 109, 0)) {
        return fail("close box should include its top-left corner", 2);
    }
    if (Leargas_Window_CloseHitTest(&w, 108, 5)) {
        return fail("one pixel left of the close box must miss", 3);
    }
    if (Leargas_Window_CloseHitTest(&w, 112, 11)) {
        return fail("one pixel below the title bar must miss", 4);
    }
    if (Leargas_Window_CloseHitTest(&w, 20, 5)) {
        return fail("title-bar left (where the title is) must miss", 5);
    }

    // Flag-gating: no close gadget without both flags.
    w.Flags = WFLG_DRAGBAR;
    if (Leargas_Window_CloseHitTest(&w, 112, 5)) {
        return fail("no close box without WFLG_CLOSEGADGET", 6);
    }
    w.Flags = WFLG_CLOSEGADGET; // no dragbar -> no title bar to host it
    if (Leargas_Window_CloseHitTest(&w, 112, 5)) {
        return fail("no close box without WFLG_DRAGBAR", 7);
    }
    if (Leargas_Window_CloseHitTest(nullptr, 0, 0)) {
        return fail("null window must miss", 8);
    }

    // ---- BuildIntuiMessage class mapping -----------------------------------
    struct Window mw = { 0 }; // WScreen null -> MouseX/Y resolve to 0
    struct IntuiMessage im;

    struct LeargasInputEvent btn = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = IECODE_LBUTTON, // == SELECTDOWN
        .ie_qualifier = 0,
    };
    Leargas_BuildIntuiMessage(&im, &mw, &btn);
    if (im.Class != IDCMP_MOUSEBUTTONS) {
        return fail("RAWMOUSE button event should map to IDCMP_MOUSEBUTTONS", 9);
    }
    if (im.Code != SELECTDOWN) {
        return fail("button Code should pass through as SELECTDOWN", 10);
    }

    struct LeargasInputEvent up = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = (u16)(IECODE_LBUTTON | IECODE_UP_PREFIX), // == SELECTUP
    };
    Leargas_BuildIntuiMessage(&im, &mw, &up);
    if (im.Class != IDCMP_MOUSEBUTTONS || im.Code != SELECTUP) {
        return fail("RAWMOUSE button-up should map to MOUSEBUTTONS/SELECTUP", 11);
    }

    struct LeargasInputEvent motion = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = IECODE_NOBUTTON,
        .ie_dx = 3,
        .ie_dy = -2,
    };
    Leargas_BuildIntuiMessage(&im, &mw, &motion);
    if (im.Class != IDCMP_MOUSEMOVE) {
        return fail("RAWMOUSE motion should map to IDCMP_MOUSEMOVE", 12);
    }

    struct LeargasInputEvent key = {
        .ie_class = IECLASS_RAWKEY,
        .ie_code = 0x20,
    };
    Leargas_BuildIntuiMessage(&im, &mw, &key);
    if (im.Class != IDCMP_RAWKEY) {
        return fail("RAWKEY should still map to IDCMP_RAWKEY", 13);
    }

    puts("leargas button ok");
    return 0;
}
