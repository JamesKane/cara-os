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

static bool iface_is_hid(const struct XhciController *xh, u32 sid, u32 j)
{
    XhciInterfaceDispatch d = xh->slots[sid].interfaces[j].dispatch;
    return d == XHCI_HID_KEYBOARD || d == XHCI_HID_MOUSE;
}

// Translate a freshly-completed HID report into Leargas input events.
static void emit_report(const struct XhciController *xh, u32 sid, u32 j, u8 *prev_buttons)
{
    const auto iface = &xh->slots[sid].interfaces[j];
    if (iface->dispatch == XHCI_HID_KEYBOARD) {
        struct CaraHidKeyboardReport kr;
        if (Croi_Hid_DecodeKeyboardBoot(iface->last_report, iface->last_report_bytes, &kr) !=
            CARA_EOK) {
            return;
        }
        // Phase 1: post the first pressed key as a down-stroke. Key-up
        // tracking + N-key rollover arrive with the Phase 3 HID Gleas;
        // editing/Return only need the down-stroke.
        u8 raw = Croi_Hid_UsageToRawKey(kr.keys[0]);
        if (raw != CARA_RAWKEY_NONE) {
            post_key(raw, kr.ie_qualifier);
        }
    } else {
        struct CaraHidMouseReport mr;
        if (Croi_Hid_DecodeMouseBoot(iface->last_report, iface->last_report_bytes, &mr) !=
            CARA_EOK) {
            return;
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

void Croi_InputPump_Task(void *arg)
{
    struct InputPumpCfg *cfg = (struct InputPumpCfg *)arg;
    if (!cfg || !cfg->xh || !cfg->pointer) {
        Croi_TaskExit();
    }
    struct XhciController *xh = cfg->xh;
    u8 prev_buttons = 0;
    for (;;) {
        // 1. Keep one interrupt-IN transfer outstanding per HID endpoint.
        for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
            if (!xh->slots[sid].in_use) {
                continue;
            }
            for (u32 j = 0; j < xh->slots[sid].n_interfaces; j++) {
                if (iface_is_hid(xh, sid, j)) {
                    (void)Croi_Xhci_HidArm(xh, (u8)sid, j);
                }
            }
        }

        // 2. Service the shared event ring; each completed transfer sets
        //    int_report_ready on its interface. Translate + route those.
        if (Croi_Xhci_HidServiceEvents(xh) > 0) {
            for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
                if (!xh->slots[sid].in_use) {
                    continue;
                }
                for (u32 j = 0; j < xh->slots[sid].n_interfaces; j++) {
                    if (iface_is_hid(xh, sid, j) && xh->slots[sid].interfaces[j].int_report_ready) {
                        xh->slots[sid].interfaces[j].int_report_ready = false;
                        emit_report(xh, sid, j, &prev_buttons);
                    }
                }
            }
            (void)Leargas_Input_Drain(cfg->pointer); // move pointer + deliver IDCMP
        }

        Croi_Yield();
    }
}
