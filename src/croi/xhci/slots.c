// SPDX-License-Identifier: BSD-2-Clause
//
// xHCI device-enumeration prologue: the per-port Reset → Enable Slot →
// Address Device chain that walks each connected USB device into the
// "Addressed" state, with EP0 (the Default Control Pipe) configured.
// After this file's work, the device is ready for standard USB §9
// requests (GET_DESCRIPTOR / SET_CONFIGURATION) which Tier 2 (UC.*)
// will drive over EP0.
//
// xHCI 1.2b §4.3 ("USB Device Initialization") is the source for the
// sequence; §6.2.5 / §6.4 define the Input Context and the TRB
// formats we build below.
//
// Phase 1 simplifications:
//   - One device per root port; no hub topology (Route String = 0).
//   - Polling event-ring servicing (no interrupts wired yet, see UB.7).
//   - One Address Device attempt per port; no retry / re-enumeration.
//   - Configure Endpoint is exposed as a primitive but only invoked
//     by Tier 2 once the HID interrupt-IN endpoint descriptor lands.

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/types.h>
#include <cara/xhci.h>

#include "internal.h"

extern struct PageAllocator g_page_alloc;

// ---- Allocation helpers ---------------------------------------------------

static u64 alloc_pages(u32 n_pages, void **kva_out)
{
    u64 phys = Page_Alloc(&g_page_alloc, n_pages);
    if (kva_out) {
        *kva_out = phys ? Mm_PhysToVirt(phys) : nullptr;
    }
    return phys;
}

// Context size in bytes — 32 for CSZ=0, 64 for CSZ=1 (HCCPARAMS1.CSZ).
// The on-disk Slot/EP context records are always 32 bytes of meaningful
// fields; the upper 32 bytes when CSZ=1 are reserved padding.
static inline u32 ctx_bytes(const struct XhciController *c)
{
    return c->csz_64 ? 64u : 32u;
}

// ---- Synchronous command issue --------------------------------------------
//
// Push one TRB onto the Command Ring, ring DB[0], poll the Event Ring
// for the matching Command Completion Event. Returns CARA_EOK iff the
// completion code is SUCCESS; on success, the caller-supplied
// out_event[4] receives the four event-TRB lanes so callers can pull
// Slot ID / CC out themselves.

static int xhci_cmd_send(struct XhciController *c, u32 p_lo, u32 p_hi, u32 status, u32 control,
                         u32 out_event[4])
{
    // 1. Place the TRB on the Command Ring with the producer cycle.
    u32 enq = c->cmd_ring_enqueue_idx;
    u32 ctrl = control | (c->cmd_ring_cycle ? XHCI_TRB_CYCLE : 0);

    // The Command Ring's TRB at the controller's current dequeue
    // matches what we're about to write — we capture its physical
    // address so we can match the resulting Command Completion Event,
    // which carries the producer-side TRB pointer in its parameter
    // field (xHCI 1.2 §6.4.2.2).
    u64 cmd_trb_phys = c->cmd_ring_phys + (u64)enq * XHCI_TRB_BYTES;

    xhci_trb_write(c->cmd_ring, enq, p_lo, p_hi, status, ctrl);

    // 2. Advance the enqueue pointer. If we just filled the slot before
    //    the Link TRB at index (size-1), bump cycle and wrap.
    enq++;
    if (enq == c->cmd_ring_size_trbs - 1u) {
        // Update the Link TRB's cycle bit so the consumer accepts it,
        // then toggle PCS (the Link TRB has TC = 1; xHCI 1.2 §4.9.2).
        u32 link_idx = c->cmd_ring_size_trbs - 1u;
        xhci_trb_write(c->cmd_ring, link_idx, (u32)(c->cmd_ring_phys & 0xFFFFFFFFu),
                       (u32)(c->cmd_ring_phys >> 32), 0,
                       XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_LINK_TC |
                           (c->cmd_ring_cycle ? XHCI_TRB_CYCLE : 0));
        enq = 0;
        c->cmd_ring_cycle = !c->cmd_ring_cycle;
    }
    c->cmd_ring_enqueue_idx = enq;

    // 3. Memory fence + ring the Command Doorbell (DB[0], target = 0).
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    xhci_doorbell_ring(c, 0, 0);

    // 4. Poll the Event Ring for our Command Completion Event. We expect
    //    one event with the matching command-TRB pointer; ignore any
    //    Port Status Change Events that may have been latched alongside.
    for (u32 i = 0; i < 1000000u; i++) {
        u32 idx = c->event_ring_dequeue_idx;
        u32 e_lo, e_hi, e_status, e_control;
        xhci_trb_read(c->event_ring, idx, &e_lo, &e_hi, &e_status, &e_control);

        // The cycle bit must match the consumer's PCS for the entry to
        // be valid (xHCI 1.2 §4.9.4.1). If not, the controller hasn't
        // posted yet — pad and retry.
        bool valid = (e_control & XHCI_TRB_CYCLE) != 0;
        if (valid != c->event_ring_cycle) {
            for (u32 j = 0; j < 100u; j++) {
                __asm__ volatile("nop");
            }
            continue;
        }

        // Advance the consumer dequeue + cycle so subsequent polls
        // see the next slot.
        u32 next = idx + 1u;
        if (next == c->event_ring_size_trbs) {
            next = 0;
            c->event_ring_cycle = !c->event_ring_cycle;
        }
        c->event_ring_dequeue_idx = next;

        // Always update ERDP so the controller knows the consumer
        // moved on (bit 3 = Event Handler Busy, written-1-to-clear).
        u64 erdp = c->event_ring_phys + (u64)c->event_ring_dequeue_idx * XHCI_TRB_BYTES;
        xhci_rt_write64_pair(c, XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERDP_LO,
                             XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERDP_HI, erdp | (1ull << 3));

        u32 trb_type = (e_control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK;
        if (trb_type == XHCI_TRB_COMMAND_COMPLETION) {
            u64 ev_trb_ptr = (u64)e_lo | ((u64)e_hi << 32);
            if (ev_trb_ptr != cmd_trb_phys) {
                // Earlier, unrelated command — keep polling.
                continue;
            }
            out_event[0] = e_lo;
            out_event[1] = e_hi;
            out_event[2] = e_status;
            out_event[3] = e_control;
            u8 cc = (u8)((e_status >> XHCI_CC_SHIFT) & XHCI_CC_MASK);
            return (cc == XHCI_CC_SUCCESS) ? CARA_EOK : CARA_EAGAIN;
        }
        // Other event types (Port Status Change, etc.) are observed but
        // not consumed beyond ERDP advancement.
    }
    return CARA_EAGAIN;
}

// ---- Port reset (xHCI 1.2 §4.19.5) ----------------------------------------
//
// USB2 root ports require software-issued reset: write PORTSC.PR = 1,
// wait for PRC = 1 (port reset change). The controller auto-enables
// (PED = 1) on successful reset and latches the negotiated speed in
// PORTSC.Port Speed. USB3 ports auto-reset on attach; if PED is
// already set we treat the port as ready and skip the reset.

static int port_reset(struct XhciController *c, u32 port_idx)
{
    u32 off = XHCI_OP_PORTSC_BASE + port_idx * XHCI_OP_PORTSC_STRIDE;
    u32 portsc = xhci_op_read32(c, off);

    if ((portsc & XHCI_PORTSC_CCS) == 0) {
        return CARA_ENOTFOUND;
    }
    if (portsc & XHCI_PORTSC_PED) {
        // USB3 path (or already-reset USB2). Just clear any latched
        // change bits so subsequent change-event polls don't misfire.
        xhci_op_write32(c, off,
                        (portsc & ~XHCI_PORTSC_RW1C_MASK) | (portsc & XHCI_PORTSC_RW1C_MASK));
        return CARA_EOK;
    }

    // Set PR while preserving non-RW1C bits and not re-asserting any
    // RW1C bits (those would clear-on-write).
    xhci_op_write32(c, off, (portsc & ~XHCI_PORTSC_RW1C_MASK) | XHCI_PORTSC_PR);

    // Wait for Port Reset Change. xHCI 1.2 §4.19.5 says reset takes
    // up to 50ms for USB2; the spin helper's nop pad is conservatively
    // longer.
    if (!xhci_spin_for_mask((volatile u32 *)(c->op_regs + off), XHCI_PORTSC_PRC, XHCI_PORTSC_PRC)) {
        LOG_ERROR("xhci", "port %u: reset did not complete (PORTSC=0x%x)", (unsigned)(port_idx + 1),
                  xhci_op_read32(c, off));
        return CARA_EAGAIN;
    }

    portsc = xhci_op_read32(c, off);
    if ((portsc & XHCI_PORTSC_PED) == 0) {
        LOG_ERROR("xhci", "port %u: reset finished but PED=0 (PORTSC=0x%x)",
                  (unsigned)(port_idx + 1), (unsigned)portsc);
        return CARA_EAGAIN;
    }

    // Clear PRC + CSC. xhci_op_write32 with the matching RW1C bits set
    // writes 1 to clear; the rest of the register is preserved.
    xhci_op_write32(c, off, (portsc & ~XHCI_PORTSC_RW1C_MASK) | XHCI_PORTSC_PRC | XHCI_PORTSC_CSC);
    return CARA_EOK;
}

// ---- Enable Slot (xHCI 1.2 §6.4.3.1) --------------------------------------
//
// No parameter; Slot Type = 0 (default USB2/USB3). Completion event
// carries the assigned Slot ID in control[31:24].

static int enable_slot(struct XhciController *c, u8 *slot_id_out)
{
    u32 ev[4];
    int rc = xhci_cmd_send(c, 0, 0, 0, XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT), ev);
    if (rc != CARA_EOK) {
        u32 cc = (ev[2] >> XHCI_CC_SHIFT) & XHCI_CC_MASK;
        LOG_ERROR("xhci", "Enable Slot failed: rc=%d CC=%u", rc, (unsigned)cc);
        return rc;
    }
    u8 slot_id = (u8)(ev[3] >> XHCI_TRB_SLOT_ID_SHIFT);
    if (slot_id == 0 || slot_id > c->max_slots || slot_id > CARA_XHCI_MAX_SLOTS) {
        LOG_ERROR("xhci", "Enable Slot returned out-of-range Slot ID %u", (unsigned)slot_id);
        return CARA_ERANGE;
    }
    *slot_id_out = slot_id;
    return CARA_EOK;
}

// ---- EP0 Max Packet Size by speed -----------------------------------------

static u32 ep0_max_packet_size(u8 speed)
{
    switch (speed) {
    case XHCI_SPEED_LOW:
        return XHCI_EP0_MAXPKT_LOW;
    case XHCI_SPEED_FULL:
        return XHCI_EP0_MAXPKT_FULL;
    case XHCI_SPEED_HIGH:
        return XHCI_EP0_MAXPKT_HIGH;
    case XHCI_SPEED_SUPER:
        return XHCI_EP0_MAXPKT_SUPER;
    case XHCI_SPEED_SUPER_PLUS:
        return XHCI_EP0_MAXPKT_SUPER;
    default:
        return XHCI_EP0_MAXPKT_FULL;
    }
}

// ---- Address Device (xHCI 1.2 §6.4.3.4) -----------------------------------
//
// 1. Allocate Output Device Context (zeroed; controller fills it in).
// 2. Allocate Input Context (Input Control + Slot + EP0).
// 3. Allocate EP0 Transfer Ring (one page, all zero; DCS=1 initial).
// 4. Build Slot Context (DW0 Speed + Context Entries = 1; DW1 Root Hub
//    Port Number = port).
// 5. Build EP0 Context (Type=Control, MaxPacket=speed-derived,
//    TR Dequeue Pointer = ep0_ring_phys | DCS, CErr=3, AvgTRBLen=8).
// 6. Set Input Control Context: Add = Slot|EP0, Drop = 0.
// 7. Install dcbaa[slot_id] = output_ctx_phys.
// 8. Send the Address Device command (BSR=0).
// 9. Read back the assigned address from output Slot Context DW3.

static int address_device(struct XhciController *c, u8 slot_id, u8 root_port, u8 speed)
{
    if (slot_id == 0 || slot_id > CARA_XHCI_MAX_SLOTS) {
        return CARA_ERANGE;
    }
    typeof(&c->slots[slot_id]) slot = &c->slots[slot_id];

    // 1. Output Device Context — one page (max 32 contexts × 64 bytes).
    void *out_kva = nullptr;
    u64 out_phys = alloc_pages(1, &out_kva);
    if (!out_phys) {
        return CARA_ENOMEM;
    }
    slot->output_ctx_phys = out_phys;
    slot->output_ctx = (volatile u8 *)out_kva;

    // 2. Input Context — one page.
    void *in_kva = nullptr;
    u64 in_phys = alloc_pages(1, &in_kva);
    if (!in_phys) {
        return CARA_ENOMEM;
    }
    slot->input_ctx_phys = in_phys;
    slot->input_ctx = (volatile u8 *)in_kva;

    // 3. EP0 Transfer Ring — one page = 256 TRBs. Cycle bit (DCS) = 1
    //    initial, matching the producer's PCS.
    void *ep0_kva = nullptr;
    u64 ep0_phys = alloc_pages(1, &ep0_kva);
    if (!ep0_phys) {
        return CARA_ENOMEM;
    }
    slot->ep0_ring_phys = ep0_phys;
    slot->ep0_ring = (volatile u32 *)ep0_kva;
    slot->ep0_ring_size_trbs = 4096u / 16u;
    slot->ep0_ring_enqueue_idx = 0;
    slot->ep0_ring_cycle = true;
    // Link TRB at last slot for wrap, same pattern as the Command Ring.
    {
        u32 last = slot->ep0_ring_size_trbs - 1u;
        xhci_trb_write(slot->ep0_ring, last, (u32)(ep0_phys & 0xFFFFFFFFu), (u32)(ep0_phys >> 32),
                       0, XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_LINK_TC);
    }

    // 3b. Per-slot DMA scratch buffer for descriptor reads (UC.*). One
    //     page is wasteful for the 18-byte device descriptor but pays
    //     off when UC.2 reads the variable-length configuration tree.
    void *dma_kva = nullptr;
    u64 dma_phys = alloc_pages(1, &dma_kva);
    if (!dma_phys) {
        return CARA_ENOMEM;
    }
    slot->dma_buf_phys = dma_phys;
    slot->dma_buf = (volatile u8 *)dma_kva;

    // 4. + 5. + 6. Build the Input Context. We treat each context block
    //    as a u32[8] array regardless of CSZ; the upper 32 bytes when
    //    CSZ=1 are reserved padding, already zero from Page_Alloc.
    u32 cb = ctx_bytes(c);

    volatile u32 *icc = (volatile u32 *)(slot->input_ctx + 0);
    volatile u32 *slot_ctx = (volatile u32 *)(slot->input_ctx + cb);
    volatile u32 *ep0_ctx = (volatile u32 *)(slot->input_ctx + 2u * cb);

    // Input Control Context: Add = Slot|EP0, Drop = 0.
    icc[0] = 0;
    icc[1] = XHCI_INPUT_CTX_ADD_SLOT | XHCI_INPUT_CTX_ADD_EP0;
    // icc[2..7] left zero.

    // Slot Context. Speed in DW0[23:20]; Context Entries in DW0[31:27]
    // (set to 1 = EP0 only). DW1 carries Root Hub Port Number in [23:16].
    slot_ctx[0] = ((u32)speed << XHCI_SLOT_CTX_DW0_SPEED_SHIFT) |
                  ((u32)1u << XHCI_SLOT_CTX_DW0_CTXENTRIES_SHIFT);
    slot_ctx[1] = ((u32)root_port << XHCI_SLOT_CTX_DW1_RHPORT_SHIFT);
    slot_ctx[2] = 0;
    slot_ctx[3] = 0;

    // EP0 Context. EP Type = 4 (Control), CErr = 3, MaxPacketSize from
    // speed; TR Dequeue Pointer = ep0_phys | DCS=1; AvgTRBLen = 8.
    u32 mps = ep0_max_packet_size(speed);
    ep0_ctx[0] = 0;
    ep0_ctx[1] = ((u32)3u << XHCI_EP_CTX_DW1_CERR_SHIFT) |
                 ((u32)XHCI_EP_TYPE_CONTROL << XHCI_EP_CTX_DW1_EPTYPE_SHIFT) |
                 (mps << XHCI_EP_CTX_DW1_MAXPKT_SHIFT);
    ep0_ctx[2] = (u32)(ep0_phys & 0xFFFFFFF0u) | XHCI_EP_CTX_DW2_DCS;
    ep0_ctx[3] = (u32)(ep0_phys >> 32);
    ep0_ctx[4] = 8u; // Average TRB Length

    // 7. Install Output Device Context in DCBAA.
    c->dcbaa[slot_id] = slot->output_ctx_phys;

    // Make sure all the structure writes are globally visible before
    // the controller starts reading them.
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    // 8. Address Device command. BSR=0 means: issue SET_ADDRESS to the
    //    device (full transition to Addressed state).
    u32 ev[4];
    int rc = xhci_cmd_send(
        c, (u32)(slot->input_ctx_phys & 0xFFFFFFFFu), (u32)(slot->input_ctx_phys >> 32), 0,
        XHCI_TRB_TYPE(XHCI_TRB_ADDRESS_DEVICE) | ((u32)slot_id << XHCI_TRB_SLOT_ID_SHIFT), ev);
    if (rc != CARA_EOK) {
        u8 cc = (u8)((ev[2] >> XHCI_CC_SHIFT) & XHCI_CC_MASK);
        LOG_ERROR("xhci", "Address Device slot=%u port=%u failed: rc=%d CC=%u", (unsigned)slot_id,
                  (unsigned)root_port, rc, (unsigned)cc);
        return rc;
    }

    // 9. Read back the controller-supplied USB Address + Slot State
    //    from the Output Slot Context (DW3).
    volatile u32 *out_slot_ctx = (volatile u32 *)(slot->output_ctx + 0);
    u32 dw3 = out_slot_ctx[3];
    slot->usb_address = (u8)(dw3 & XHCI_SLOT_CTX_DW3_ADDRESS_MASK);
    slot->slot_state = (u8)((dw3 >> XHCI_SLOT_CTX_DW3_STATE_SHIFT) & XHCI_SLOT_CTX_DW3_STATE_MASK);
    slot->root_port = root_port;
    slot->speed = speed;
    slot->in_use = true;
    return CARA_EOK;
}

// ---- Public entry points --------------------------------------------------

[[nodiscard]] int Croi_Xhci_AddressConnectedDevices(struct XhciController *c)
{
    if (!c || !c->running) {
        return CARA_EINVAL;
    }
    if (c->page_size_bytes != 4096) {
        return CARA_ERANGE;
    }

    c->n_addressed_slots = 0;

    for (u32 i = 0; i < c->max_ports; i++) {
        if (!c->port[i].connected) {
            continue;
        }

        int rc = port_reset(c, i);
        if (rc != CARA_EOK) {
            LOG_WARN("xhci", "port %u reset skipped/failed (%d)", (unsigned)(i + 1), rc);
            continue;
        }

        // Re-read PORTSC after reset to capture the negotiated speed
        // (USB2 ports populate Port Speed only after PED settles).
        u32 portsc = xhci_op_read32(c, XHCI_OP_PORTSC_BASE + i * XHCI_OP_PORTSC_STRIDE);
        u8 speed = (u8)((portsc >> XHCI_PORTSC_SPEED_SHIFT) & XHCI_PORTSC_SPEED_MASK);
        c->port[i].enabled = (portsc & XHCI_PORTSC_PED) != 0;
        c->port[i].speed = speed;
        c->port[i].link_state = (u8)((portsc >> XHCI_PORTSC_PLS_SHIFT) & XHCI_PORTSC_PLS_MASK);

        u8 slot_id = 0;
        rc = enable_slot(c, &slot_id);
        if (rc != CARA_EOK) {
            continue;
        }

        rc = address_device(c, slot_id, (u8)(i + 1), speed);
        if (rc != CARA_EOK) {
            continue;
        }

        c->n_addressed_slots++;
        LOG_INFO("xhci", "addressed slot=%u port=%u speed=%u usb-addr=%u state=%u",
                 (unsigned)slot_id, (unsigned)(i + 1), (unsigned)speed,
                 (unsigned)c->slots[slot_id].usb_address, (unsigned)c->slots[slot_id].slot_state);
    }

    LOG_INFO("xhci", "%u of %u connected ports addressed", (unsigned)c->n_addressed_slots,
             (unsigned)c->n_connected_ports);
    return CARA_EOK;
}

[[nodiscard]] int Croi_Xhci_ConfigureEndpoint(struct XhciController *c, u8 slot_id,
                                              u64 input_ctx_phys)
{
    if (!c || !c->running || slot_id == 0 || slot_id > CARA_XHCI_MAX_SLOTS) {
        return CARA_EINVAL;
    }
    if (!c->slots[slot_id].in_use) {
        return CARA_EINVAL;
    }
    u32 ev[4];
    int rc = xhci_cmd_send(
        c, (u32)(input_ctx_phys & 0xFFFFFFFFu), (u32)(input_ctx_phys >> 32), 0,
        XHCI_TRB_TYPE(XHCI_TRB_CONFIGURE_ENDPOINT) | ((u32)slot_id << XHCI_TRB_SLOT_ID_SHIFT), ev);
    if (rc != CARA_EOK) {
        u8 cc = (u8)((ev[2] >> XHCI_CC_SHIFT) & XHCI_CC_MASK);
        LOG_ERROR("xhci", "Configure Endpoint slot=%u failed: rc=%d CC=%u", (unsigned)slot_id, rc,
                  (unsigned)cc);
        return rc;
    }
    // Update cached slot_state from Output Slot Context.
    volatile u32 *out_slot_ctx = (volatile u32 *)(c->slots[slot_id].output_ctx + 0);
    u32 dw3 = out_slot_ctx[3];
    c->slots[slot_id].slot_state =
        (u8)((dw3 >> XHCI_SLOT_CTX_DW3_STATE_SHIFT) & XHCI_SLOT_CTX_DW3_STATE_MASK);
    return CARA_EOK;
}
