// SPDX-License-Identifier: BSD-2-Clause
//
// Interface dispatch (UC.4). Walks every parsed interface across every
// in-use slot and classifies it into XhciInterfaceDispatch. The
// decision drives whether (eventually) a Tier 3 Gleas Croi spawns
// against the device — for Phase 1 only HID/Boot keyboard / mouse are
// supported, and the dispatch decision is recorded but no Gleas spawn
// happens until Tier 3 lands the HID class driver.

#include <cara/log.h>
#include <cara/types.h>
#include <cara/usb.h>
#include <cara/xhci.h>

static const char *dispatch_name(XhciInterfaceDispatch d)
{
    switch (d) {
    case XHCI_HID_KEYBOARD:     return "HID/Boot/Keyboard";
    case XHCI_HID_MOUSE:        return "HID/Boot/Mouse";
    case XHCI_HID_OTHER:        return "HID-other";
    case XHCI_DISP_UNSUPPORTED: return "unsupported";
    default:                    return "none";
    }
}

[[nodiscard]] int Croi_Xhci_DispatchInterfaces(struct XhciController *c)
{
    if (!c) {
        return CARA_EINVAL;
    }
    c->n_hid_keyboards = 0;
    c->n_hid_mice      = 0;

    for (u32 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!c->slots[sid].in_use) {
            continue;
        }
        for (u32 j = 0; j < c->slots[sid].n_interfaces; j++) {
            auto iface = &c->slots[sid].interfaces[j];
            if (!iface->valid) {
                continue;
            }
            XhciInterfaceDispatch d;
            if (iface->bInterfaceClass == USB_CLASS_HID) {
                if (iface->bInterfaceSubClass == USB_HID_SUBCLASS_BOOT) {
                    if (iface->bInterfaceProtocol
                        == USB_HID_PROTOCOL_KEYBOARD) {
                        d = XHCI_HID_KEYBOARD;
                        c->n_hid_keyboards++;
                    } else if (iface->bInterfaceProtocol
                               == USB_HID_PROTOCOL_MOUSE) {
                        d = XHCI_HID_MOUSE;
                        c->n_hid_mice++;
                    } else {
                        d = XHCI_HID_OTHER;
                    }
                } else {
                    d = XHCI_HID_OTHER;
                }
            } else {
                d = XHCI_DISP_UNSUPPORTED;
            }
            iface->dispatch = d;

            if (d == XHCI_DISP_UNSUPPORTED) {
                LOG_INFO("xhci",
                         "slot=%u iface[%u] class=%u/%u/%u: unsupported in Phase 1",
                         (unsigned)sid, (unsigned)j,
                         (unsigned)iface->bInterfaceClass,
                         (unsigned)iface->bInterfaceSubClass,
                         (unsigned)iface->bInterfaceProtocol);
            } else {
                LOG_INFO("xhci",
                         "slot=%u iface[%u] dispatch=%s",
                         (unsigned)sid, (unsigned)j,
                         dispatch_name(d));
            }
        }
    }

    LOG_INFO("xhci",
             "dispatch totals: HID keyboards=%u HID mice=%u",
             (unsigned)c->n_hid_keyboards,
             (unsigned)c->n_hid_mice);
    return CARA_EOK;
}
