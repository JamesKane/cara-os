// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel input-pump task (see input_pump.h). One round of poll_hid reads
// each HID/Boot interrupt endpoint once with a short spin budget; new
// reports are decoded and posted to the Leargas L0 ring. Keyboard reports
// post the first pressed key (RAWKEY); mouse reports post relative motion
// and left-button down/up *edges* (so a click becomes SELECTDOWN then
// SELECTUP). After each round the ring is drained, driving the pointer
// and delivering IDCMP to the focused window.

#include "input_pump.h"

#include <cara/hid.h>
#include <cara/leargas.h>
#include <cara/sched.h>
#include <cara/time.h>
#include <cara/types.h>
#include <cara/xhci.h>
#include <devices/inputevent.h>

// Per-endpoint spin budget. Croi_Xhci_HidIntReadOnce enqueues a fresh
// IN transfer and polls the event ring for its completion, so the budget
// must be long enough that an asynchronously-arriving report lands inside
// the same call (a too-short budget times out and the report completes
// against a later call's wait, getting dropped). Clar blocks in WaitPort
// when idle, so a long busy-wait here doesn't starve it.
//
// NOTE: live HID delivery under QEMU is not yet confirmed end-to-end —
// injected reports (monitor mouse_move / sendkey) don't reach the pump,
// so this value is provisional until the xHCI interrupt-IN path is
// debugged (likely needs a persistent outstanding transfer rather than
// re-enqueue-per-poll). The full Clar interaction is covered meanwhile by
// the deterministic KERNEL_TEST(clar_smoke).
#define PUMP_READ_ITERS 50000u

static void post_key(u8 raw, u16 qual)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWKEY,
        .ie_code = raw,
        .ie_qualifier = qual,
        .ie_ts_ns = Croi_Time_Now(),
    };
    (void)Leargas_Input_Post(&ev);
}

static void post_mouse(u16 code, u16 qual, i16 dx, i16 dy)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWMOUSE,
        .ie_code = code,
        .ie_qualifier = qual,
        .ie_dx = dx,
        .ie_dy = dy,
        .ie_ts_ns = Croi_Time_Now(),
    };
    (void)Leargas_Input_Post(&ev);
}

static void poll_hid_round(struct XhciController *xh, u8 *prev_buttons)
{
    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!xh->slots[sid].in_use) {
            continue;
        }
        for (u32 j = 0; j < xh->slots[sid].n_interfaces; j++) {
            const auto iface = &xh->slots[sid].interfaces[j];
            if (iface->dispatch != XHCI_HID_KEYBOARD && iface->dispatch != XHCI_HID_MOUSE) {
                continue;
            }
            u32 got = 0;
            if (Croi_Xhci_HidIntReadOnce(xh, (u8)sid, j, PUMP_READ_ITERS, &got) != CARA_EOK) {
                continue; // no new report this round (endpoint idle)
            }

            if (iface->dispatch == XHCI_HID_KEYBOARD) {
                struct CaraHidKeyboardReport kr;
                if (Croi_Hid_DecodeKeyboardBoot(iface->last_report, iface->last_report_bytes,
                                                &kr) != CARA_EOK) {
                    continue;
                }
                // Phase 1: post the first pressed key as a down-stroke.
                // Key-up tracking + N-key rollover arrive with the Phase 3
                // HID Gleas; editing/Return only need the down-stroke.
                u8 raw = Croi_Hid_UsageToRawKey(kr.keys[0]);
                if (raw != CARA_RAWKEY_NONE) {
                    post_key(raw, kr.ie_qualifier);
                }
            } else {
                struct CaraHidMouseReport mr;
                if (Croi_Hid_DecodeMouseBoot(iface->last_report, iface->last_report_bytes, &mr) !=
                    CARA_EOK) {
                    continue;
                }
                // Motion first, so a click lands at the moved-to position.
                if (mr.dx != 0 || mr.dy != 0) {
                    post_mouse(IECODE_NOBUTTON, mr.ie_qualifier, (i16)mr.dx, (i16)mr.dy);
                }
                // Left-button edges -> SELECTDOWN / SELECTUP.
                u8 now = (u8)(mr.buttons & HID_MOUSE_BTN_LEFT);
                u8 was = (u8)(*prev_buttons & HID_MOUSE_BTN_LEFT);
                if (now && !was) {
                    post_mouse(IECODE_LBUTTON, mr.ie_qualifier, 0, 0);
                } else if (!now && was) {
                    post_mouse((u16)(IECODE_LBUTTON | IECODE_UP_PREFIX), mr.ie_qualifier, 0, 0);
                }
                *prev_buttons = mr.buttons;
            }
        }
    }
}

void Croi_InputPump_Task(void *arg)
{
    struct InputPumpCfg *cfg = (struct InputPumpCfg *)arg;
    if (!cfg || !cfg->xh || !cfg->pointer) {
        Croi_TaskExit();
    }
    u8 prev_buttons = 0;
    for (;;) {
        poll_hid_round(cfg->xh, &prev_buttons);
        (void)Leargas_Input_Drain(cfg->pointer);
        Croi_Yield();
    }
}
