// SPDX-License-Identifier: BSD-2-Clause
//
// xHCI controller probe + reset. Phase 1 first cut: walk the PCI
// function for an xHCI device, size + allocate BAR0, parse the
// capability registers, run USBCMD.HCRST until CNR clears.
//
// What this does NOT yet do (follow-on commits):
//   - Allocate the DCBAA + scratchpad buffers
//   - Set up the Command Ring and Event Ring (with ERST)
//   - Drive Run/Stop = 1 and walk PORTSC for connected devices
//   - Issue Enable Slot / Address Device commands
//
// xHCI 1.2b (eXtensible Host Controller Interface, USB-IF) is the
// source for every register / field / sequence below. Section
// references in comments are to that document.

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/pci.h>
#include <cara/types.h>
#include <cara/xhci.h>

// ---- MMIO helpers ----------------------------------------------------------
//
// xHCI register access is 32-bit only on most controllers (QEMU's
// qemu-xhci returns zero for narrow reads). The u8/u16 helpers
// below do an aligned u32 load and shift the requested lane out;
// callers don't have to know.

static u32 cap_read32_aligned(const struct XhciController *c, u32 off)
{
    return *(volatile u32 *)(c->cap_regs + (off & ~3u));
}

static u8 cap_read8(const struct XhciController *c, u32 off)
{
    u32 word = cap_read32_aligned(c, off);
    return (u8)(word >> ((off & 3u) * 8u));
}

static u16 cap_read16(const struct XhciController *c, u32 off)
{
    // Off must be 2-byte aligned. xHCI lays its 16-bit fields out
    // that way (HCIVERSION at 0x02, port speed major/minor at
    // capability blocks, etc.).
    u32 word = cap_read32_aligned(c, off);
    return (u16)(word >> ((off & 2u) * 8u));
}

static u32 cap_read32(const struct XhciController *c, u32 off)
{
    return *(volatile u32 *)(c->cap_regs + off);
}

static u32 op_read32(const struct XhciController *c, u32 off)
{
    return *(volatile u32 *)(c->op_regs + off);
}

static void op_write32(const struct XhciController *c, u32 off, u32 v)
{
    *(volatile u32 *)(c->op_regs + off) = v;
}

// Spin up to ~1M iterations waiting for `(*reg & mask) == expect`.
// Returns false on timeout. Each iteration reads MMIO, so this
// equates to milliseconds of real time on the daily-driver targets.
[[nodiscard]] static bool
spin_for_mask(volatile u32 *reg, u32 mask, u32 expect)
{
    for (u32 i = 0; i < 1000000u; i++) {
        if ((*reg & mask) == expect) {
            return true;
        }
        // Friendly to the hardware: a small hint to back off a bit.
        for (u32 j = 0; j < 100u; j++) {
            __asm__ volatile("nop");
        }
    }
    return false;
}

// ---- Probe -----------------------------------------------------------------

[[nodiscard]] int Croi_Xhci_Probe(struct XhciController *out,
                                  struct PciInventory *inv,
                                  u32 func_index)
{
    if (!out || !inv || func_index >= inv->n_funcs) {
        return CARA_EINVAL;
    }
    *out = (struct XhciController){ 0 };

    struct PciFunction *fn = &inv->func[func_index];
    if (fn->base_class != PCI_CLASS_USB_XHCI_BASE
        || fn->subclass != PCI_CLASS_USB_XHCI_SUB
        || fn->prog_if  != PCI_CLASS_USB_XHCI_PROGIF) {
        LOG_ERROR("xhci",
                  "function %x is not class 0x0c/0x03/0x30 (got %02x.%02x.%02x)",
                  ((u32)fn->bus << 16) | ((u32)fn->device << 8) | fn->function,
                  (unsigned)fn->base_class, (unsigned)fn->subclass,
                  (unsigned)fn->prog_if);
        return CARA_EINVAL;
    }

    // Stash PCI identity for later log output.
    out->pci_bus      = fn->bus;
    out->pci_device   = fn->device;
    out->pci_function = fn->function;
    out->vendor_id    = fn->vendor_id;
    out->device_id    = fn->device_id;

    // Allocate + program BAR0.
    int rc = Croi_Pci_AllocateBar(inv, func_index, 0);
    if (rc != CARA_EOK) {
        LOG_ERROR("xhci", "BAR0 allocation failed: %d", rc);
        return rc;
    }
    out->bar0_phys = fn->bar[0].base;
    out->bar0_size = fn->bar[0].size;

    // BAR0 lives in the host bridge's MEM32 range, which Croi_Pci_Init
    // already pinned into the boot PT as a 1 GiB device leaf. The
    // upper-half direct-map alias is reachable.
    out->cap_regs = (volatile u8 *)Mm_PhysToVirt(out->bar0_phys);

    // Capability registers: parse CAPLENGTH + HCIVERSION first to
    // anchor every other offset.
    u8 caplen = cap_read8(out, XHCI_CAP_CAPLENGTH);
    u16 ver   = cap_read16(out, XHCI_CAP_HCIVERSION);
    out->hci_version = ver;
    if (ver < 0x0100) {
        LOG_ERROR("xhci", "HCIVERSION 0x%x < 0x100 (pre-xHCI 1.0); refusing",
                  (unsigned)ver);
        return CARA_EBADVERSION;
    }

    out->op_regs = out->cap_regs + caplen;

    u32 hcsp1 = cap_read32(out, XHCI_CAP_HCSPARAMS1);
    out->max_slots = (hcsp1 >> XHCI_HCSP1_MAX_SLOTS_SHIFT)
                     & XHCI_HCSP1_MAX_SLOTS_MASK;
    out->max_intrs = (hcsp1 >> XHCI_HCSP1_MAX_INTRS_SHIFT)
                     & XHCI_HCSP1_MAX_INTRS_MASK;
    out->max_ports = (hcsp1 >> XHCI_HCSP1_MAX_PORTS_SHIFT)
                     & XHCI_HCSP1_MAX_PORTS_MASK;

    u32 hcsp2 = cap_read32(out, XHCI_CAP_HCSPARAMS2);
    u32 scr_hi = (hcsp2 >> XHCI_HCSP2_SCR_HI_SHIFT) & XHCI_HCSP2_SCR_HI_MASK;
    u32 scr_lo = (hcsp2 >> XHCI_HCSP2_SCR_LO_SHIFT) & XHCI_HCSP2_SCR_LO_MASK;
    out->max_scratchpad_buffers = (scr_hi << 5) | scr_lo;

    u32 hccp1 = cap_read32(out, XHCI_CAP_HCCPARAMS1);
    out->ac64    = (hccp1 & XHCI_HCCP1_AC64) != 0;
    out->csz_64  = (hccp1 & XHCI_HCCP1_CSZ)  != 0;

    u32 dboff  = cap_read32(out, XHCI_CAP_DBOFF)  & ~0x3u;
    u32 rtsoff = cap_read32(out, XHCI_CAP_RTSOFF) & ~0x1Fu;
    out->doorbells    = (volatile u32 *)(out->cap_regs + dboff);
    out->runtime_regs = out->cap_regs + rtsoff;

    out->page_size_bytes = op_read32(out, XHCI_OP_PAGESIZE) << 12;

    LOG_INFO("xhci",
             "v%x.%x bar0=0x%llx (%llu KiB) caplen=%u slots=%u intrs=%u ports=%u",
             (unsigned)((ver >> 8) & 0xFF), (unsigned)(ver & 0xFF),
             (u64)out->bar0_phys, (u64)(out->bar0_size >> 10),
             (unsigned)caplen,
             (unsigned)out->max_slots,
             (unsigned)out->max_intrs,
             (unsigned)out->max_ports);
    LOG_INFO("xhci",
             "scratchpad-bufs=%u page-size=%u %s%s",
             (unsigned)out->max_scratchpad_buffers,
             (unsigned)out->page_size_bytes,
             out->ac64   ? "AC64 " : "",
             out->csz_64 ? "CSZ64" : "CSZ32");

    // ---- Reset (xHCI 1.2 §4.2) ---------------------------------------------
    //
    //   1. Stop the controller (USBCMD.RS = 0).
    //   2. Wait USBSTS.HCH = 1 (halted).
    //   3. Set USBCMD.HCRST = 1.
    //   4. Wait until USBCMD.HCRST clears (controller has finished
    //      the reset; this can take up to 250 ms per the spec).
    //   5. Wait until USBSTS.CNR clears (the controller is ready
    //      to accept commands again).

    u32 cmd = op_read32(out, XHCI_OP_USBCMD);
    if (cmd & XHCI_USBCMD_RUN) {
        cmd &= ~XHCI_USBCMD_RUN;
        op_write32(out, XHCI_OP_USBCMD, cmd);
    }
    if (!spin_for_mask((volatile u32 *)(out->op_regs + XHCI_OP_USBSTS),
                       XHCI_USBSTS_HCH, XHCI_USBSTS_HCH)) {
        LOG_ERROR("xhci", "controller did not halt (USBSTS.HCH never set)");
        return CARA_EAGAIN;
    }

    cmd = op_read32(out, XHCI_OP_USBCMD);
    op_write32(out, XHCI_OP_USBCMD, cmd | XHCI_USBCMD_HCRST);
    if (!spin_for_mask((volatile u32 *)(out->op_regs + XHCI_OP_USBCMD),
                       XHCI_USBCMD_HCRST, 0)) {
        LOG_ERROR("xhci", "HCRST did not self-clear");
        return CARA_EAGAIN;
    }
    if (!spin_for_mask((volatile u32 *)(out->op_regs + XHCI_OP_USBSTS),
                       XHCI_USBSTS_CNR, 0)) {
        LOG_ERROR("xhci", "USBSTS.CNR did not clear after reset");
        return CARA_EAGAIN;
    }

    LOG_INFO("xhci", "reset complete; controller idle (USBSTS=0x%x)",
             op_read32(out, XHCI_OP_USBSTS));
    return CARA_EOK;
}
