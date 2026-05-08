// SPDX-License-Identifier: BSD-2-Clause
//
// PORTSC walker (xHCI 1.2 §4.19 / §5.4.8). Reads each port's
// status register and records connect / enable / speed / link
// state in c->port[]. The Phase 1 first cut is observe-only; it
// neither powers ports up (QEMU's qemu-xhci already powers them;
// real silicon's per-port PMIC does the same before the OS sees
// the hardware) nor issues port-reset commands (Reset is part of
// device-enumeration in UB.5).

#include <cara/log.h>
#include <cara/types.h>
#include <cara/xhci.h>

#include "internal.h"

static const char *speed_name(u8 speed)
{
    switch (speed) {
    case XHCI_SPEED_FULL:        return "FS";
    case XHCI_SPEED_LOW:         return "LS";
    case XHCI_SPEED_HIGH:        return "HS";
    case XHCI_SPEED_SUPER:       return "SS";
    case XHCI_SPEED_SUPER_PLUS:  return "SS+";
    default:                     return "?";
    }
}

[[nodiscard]] int Croi_Xhci_PortsWalk(struct XhciController *c)
{
    if (!c || !c->op_regs) {
        return CARA_EINVAL;
    }
    if (!c->running) {
        // The controller doesn't latch CCS until it's running; a
        // walk before then would just see all-zeros.
        return CARA_EINVAL;
    }
    if (c->max_ports > 256) {
        // The fixed-size port[] in struct XhciController is sized
        // for the xHCI maximum of 255 ports; refuse if HCSPARAMS1
        // somehow advertises more.
        return CARA_ERANGE;
    }

    c->n_connected_ports = 0;

    for (u32 i = 0; i < c->max_ports; i++) {
        u32 off = XHCI_OP_PORTSC_BASE + i * XHCI_OP_PORTSC_STRIDE;
        u32 portsc = xhci_op_read32(c, off);

        bool connected = (portsc & XHCI_PORTSC_CCS) != 0;
        bool enabled   = (portsc & XHCI_PORTSC_PED) != 0;
        u8   speed     = (u8)((portsc >> XHCI_PORTSC_SPEED_SHIFT)
                              & XHCI_PORTSC_SPEED_MASK);
        u8   pls       = (u8)((portsc >> XHCI_PORTSC_PLS_SHIFT)
                              & XHCI_PORTSC_PLS_MASK);

        c->port[i].connected  = connected;
        c->port[i].enabled    = enabled;
        c->port[i].speed      = speed;
        c->port[i].link_state = pls;

        if (connected) {
            c->n_connected_ports++;
            LOG_INFO("xhci",
                     "port %u: connected speed=%s pls=%u portsc=0x%x",
                     (unsigned)(i + 1),       // 1-based for the human-facing log
                     speed_name(speed),
                     (unsigned)pls,
                     (unsigned)portsc);
        }
    }

    LOG_INFO("xhci", "%u of %u ports connected",
             (unsigned)c->n_connected_ports,
             (unsigned)c->max_ports);
    return CARA_EOK;
}
