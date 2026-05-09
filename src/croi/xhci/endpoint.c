// SPDX-License-Identifier: BSD-2-Clause
//
// Configure Endpoint Command (UC.5). Adds an Endpoint Context to a
// Device Slot so the xHC knows about a non-EP0 endpoint. Phase 1
// only handles HID interrupt-IN endpoints — the Tier 3 HID Gleas
// will eventually enqueue Normal IN TRBs onto the rings UC.5 sets up
// here, and the controller will DMA boot-protocol report bytes into
// our buffers.
//
// xHCI 1.2 §4.3.5 / §4.6.6 / §6.2.3 are the source.

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/types.h>
#include <cara/usb.h>
#include <cara/xhci.h>

#include "internal.h"

extern struct PageAllocator g_page_alloc;

static u64 alloc_pages(u32 n_pages, void **kva_out)
{
    u64 phys = Page_Alloc(&g_page_alloc, n_pages);
    if (kva_out) {
        *kva_out = phys ? Mm_PhysToVirt(phys) : nullptr;
    }
    return phys;
}

static inline u32 ctx_bytes(const struct XhciController *c)
{
    return c->csz_64 ? 64u : 32u;
}

// USB §9.6.6 → xHCI §4.5.1 endpoint Device Context Index.
//   EP0:        DCI = 1
//   EP_N OUT:   DCI = 2*N
//   EP_N IN:    DCI = 2*N + 1
static u32 endpoint_dci(u8 ep_address)
{
    u8 ep_num = (u8)(ep_address & 0x0Fu);
    bool is_in = (ep_address & USB_EP_DIR_IN) != 0;
    return (u32)(2u * (u32)ep_num + (is_in ? 1u : 0u));
}

// xHCI 1.2 §6.2.3.6 — encode an interrupt endpoint's poll period.
// USB HS/SS interrupt: bInterval ∈ [1,16], poll = 2^(bInterval-1)
//   microframes; xHCI Interval = bInterval - 1.
// USB FS/LS interrupt: bInterval is in 1ms frames (8 microframes
//   each); xHCI Interval = ceil(log2(bInterval * 8)).
static u8 hid_interval_for_speed(u8 speed, u8 b_interval)
{
    switch (speed) {
    case XHCI_SPEED_HIGH:
    case XHCI_SPEED_SUPER:
    case XHCI_SPEED_SUPER_PLUS:
        return (b_interval > 0) ? (u8)(b_interval - 1) : 0;

    case XHCI_SPEED_FULL:
    case XHCI_SPEED_LOW: {
        if (b_interval == 0) {
            return 3; // 1ms ≈ 8 microframes ≈ 2^3
        }
        u32 microframes = (u32)b_interval * 8u;
        u8 log2 = 0;
        while ((1u << log2) < microframes && log2 < 15) {
            log2++;
        }
        return log2;
    }
    default:
        return 3;
    }
}

static int configure_one_int_endpoint(struct XhciController *c, u8 slot_id, u32 iface_idx)
{
    typeof(&c->slots[slot_id]) slot = &c->slots[slot_id];
    typeof(&slot->interfaces[iface_idx]) iface = &slot->interfaces[iface_idx];

    if (!iface->valid || !iface->ep_present) {
        return CARA_EINVAL;
    }
    u32 dci = endpoint_dci(iface->ep_address);
    if (dci < 2 || dci > 31) {
        // DCI 0 is the Slot Context, 1 is EP0; legitimate device
        // endpoints occupy 2..31 (xHCI 1.2 §4.5.1).
        LOG_ERROR("xhci", "slot=%u iface[%u] illegal DCI %u for ep_addr=0x%x", (unsigned)slot_id,
                  (unsigned)iface_idx, (unsigned)dci, (unsigned)iface->ep_address);
        return CARA_ERANGE;
    }

    // 1. Allocate the interrupt-IN Transfer Ring + its Link TRB.
    void *ring_kva = nullptr;
    u64 ring_phys = alloc_pages(1, &ring_kva);
    if (!ring_phys) {
        return CARA_ENOMEM;
    }
    iface->int_ring_phys = ring_phys;
    iface->int_ring = (volatile u32 *)ring_kva;
    iface->int_ring_size_trbs = 4096u / 16u;
    iface->int_ring_enqueue_idx = 0;
    iface->int_ring_cycle = true;
    iface->int_ep_dci = (u8)dci;
    {
        u32 last = iface->int_ring_size_trbs - 1u;
        xhci_trb_write(iface->int_ring, last, (u32)(ring_phys & 0xFFFFFFFFu),
                       (u32)(ring_phys >> 32), 0, XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_LINK_TC);
    }

    // 1b. Allocate the per-interface interrupt-IN scratch buffer.
    //     One page is overkill for an 8-byte boot report but keeps
    //     allocation / alignment uniform with the rest of the per-slot
    //     DMA surface.
    void *int_buf_kva = nullptr;
    u64 int_buf_phys = alloc_pages(1, &int_buf_kva);
    if (!int_buf_phys) {
        return CARA_ENOMEM;
    }
    iface->int_buf_phys = int_buf_phys;
    iface->int_buf = (volatile u8 *)int_buf_kva;
    iface->last_report_bytes = 0;

    // 2. Zero the (reused) Input Context page; the Address Device
    //    fill is stale at this point and the xHC reads any non-zero
    //    bits we've left behind.
    for (u32 i = 0; i < 4096u; i++) {
        slot->input_ctx[i] = 0;
    }

    u32 cb = ctx_bytes(c);
    volatile u32 *icc = (volatile u32 *)(slot->input_ctx + 0);
    volatile u32 *slot_ctx = (volatile u32 *)(slot->input_ctx + cb);
    volatile u32 *ep_ctx = (volatile u32 *)(slot->input_ctx + (1u + dci) * cb);

    // Input Control Context. Drop=0; Add = A0 (Slot Context) | A_DCI.
    // We need A0 because we're updating the Slot Context's
    // Context Entries field to encompass the new endpoint.
    icc[0] = 0;
    icc[1] = XHCI_INPUT_CTX_ADD_SLOT | (1u << dci);

    // Slot Context. Speed and Root Hub Port Number from the cached
    // post-Address Device state; Context Entries bumped to the highest
    // DCI we're now using. USB Address / Slot State are xHC-controlled
    // (DW3) and software shall write 0 (xHCI 1.2 §6.2.2 / §4.6.6).
    slot_ctx[0] = ((u32)slot->speed << XHCI_SLOT_CTX_DW0_SPEED_SHIFT) |
                  ((u32)dci << XHCI_SLOT_CTX_DW0_CTXENTRIES_SHIFT);
    slot_ctx[1] = ((u32)slot->root_port << XHCI_SLOT_CTX_DW1_RHPORT_SHIFT);

    // EP_DCI Context. INTERRUPT_IN, CErr=3, MaxPacketSize from the
    // descriptor, Interval encoded for the slot's speed.
    u32 mps = iface->ep_max_packet;
    u8 interval = hid_interval_for_speed(slot->speed, iface->ep_interval);
    ep_ctx[0] = ((u32)interval << XHCI_EP_CTX_DW0_INTERVAL_SHIFT);
    ep_ctx[1] = ((u32)3u << XHCI_EP_CTX_DW1_CERR_SHIFT) |
                ((u32)XHCI_EP_TYPE_INTERRUPT_IN << XHCI_EP_CTX_DW1_EPTYPE_SHIFT) |
                (mps << XHCI_EP_CTX_DW1_MAXPKT_SHIFT);
    ep_ctx[2] = (u32)(ring_phys & 0xFFFFFFF0u) | XHCI_EP_CTX_DW2_DCS;
    ep_ctx[3] = (u32)(ring_phys >> 32);
    // Average TRB Length: a Normal-In TRB carries one mps-sized read
    // per interrupt poll. Setting AvgTRBLen = mps keeps the controller's
    // bandwidth accounting honest enough for boot-protocol HID.
    ep_ctx[4] = mps;

    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    // 3. Issue the Configure Endpoint Command.
    int rc = Croi_Xhci_ConfigureEndpoint(c, slot_id, slot->input_ctx_phys);
    if (rc != CARA_EOK) {
        return rc;
    }

    // 4. Read back Slot State from the Output Slot Context. On a
    //    successful Configure Endpoint with one or more Add bits set,
    //    Slot State becomes Configured (3) — xHCI 1.2 §4.5.3.
    volatile u32 *out_slot_ctx = (volatile u32 *)(slot->output_ctx + 0);
    u32 dw3 = out_slot_ctx[3];
    slot->slot_state = (u8)((dw3 >> XHCI_SLOT_CTX_DW3_STATE_SHIFT) & XHCI_SLOT_CTX_DW3_STATE_MASK);

    iface->ep_xhci_configured = true;
    return CARA_EOK;
}

[[nodiscard]] int Croi_Xhci_ConfigureHidInterrupts(struct XhciController *c)
{
    if (!c || !c->running) {
        return CARA_EINVAL;
    }

    c->n_xhci_configured_interfaces = 0;
    for (u8 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!c->slots[sid].in_use) {
            continue;
        }
        for (u32 j = 0; j < c->slots[sid].n_interfaces; j++) {
            typeof(&c->slots[sid].interfaces[j]) iface = &c->slots[sid].interfaces[j];
            if (!iface->valid || !iface->ep_present) {
                continue;
            }
            if (iface->dispatch != XHCI_HID_KEYBOARD && iface->dispatch != XHCI_HID_MOUSE) {
                continue;
            }
            if (iface->ep_xhci_configured) {
                c->n_xhci_configured_interfaces++;
                continue;
            }
            int rc = configure_one_int_endpoint(c, sid, j);
            if (rc != CARA_EOK) {
                LOG_WARN("xhci", "slot=%u iface[%u] Configure Endpoint failed: %d", (unsigned)sid,
                         (unsigned)j, rc);
                continue;
            }
            LOG_INFO("xhci", "slot=%u iface[%u] %s int-IN dci=%u ring=0x%llx slot_state=%u",
                     (unsigned)sid, (unsigned)j,
                     (iface->dispatch == XHCI_HID_KEYBOARD) ? "kbd" : "mouse",
                     (unsigned)iface->int_ep_dci, (u64)iface->int_ring_phys,
                     (unsigned)c->slots[sid].slot_state);
            c->n_xhci_configured_interfaces++;
        }
    }
    LOG_INFO("xhci", "%u HID interrupt-IN endpoints xHCI-configured",
             (unsigned)c->n_xhci_configured_interfaces);
    return CARA_EOK;
}

// ---- Interrupt-IN read primitive ------------------------------------------
//
// Single-shot poll over an already-configured HID interrupt-IN endpoint.
// Phase 1 doesn't have a real interrupter — the eventual UB.7 work will
// install an interrupt handler that drains Transfer Events into a queue
// the Tier 3 HID Gleas reads from. For boot-time bring-up we do the
// simplest thing: enqueue one Normal TRB, ring the doorbell, poll for
// the matching Transfer Event with a short timeout.
//
// The TRB length we ask for is the endpoint's MaxPacketSize. Boot
// reports are 8 bytes (kbd) or 3-4 bytes (mouse); USB short-packet
// completion (CC=13) handles the residue cleanly.

[[nodiscard]] int Croi_Xhci_HidIntReadOnce(struct XhciController *c, u8 slot_id, u32 iface_idx,
                                           u32 timeout_iters, u32 *bytes_received_out)
{
    if (bytes_received_out) {
        *bytes_received_out = 0;
    }
    if (!c || !c->running || slot_id == 0 || slot_id > CARA_XHCI_MAX_SLOTS ||
        iface_idx >= CARA_XHCI_MAX_INTERFACES_PER_SLOT) {
        return CARA_EINVAL;
    }
    typeof(&c->slots[slot_id]) slot = &c->slots[slot_id];
    typeof(&slot->interfaces[iface_idx]) iface = &slot->interfaces[iface_idx];
    if (!iface->valid || !iface->ep_present || !iface->ep_xhci_configured || !iface->int_buf ||
        !iface->int_ring) {
        return CARA_EINVAL;
    }

    // Zero the scratch buffer — the controller writes exactly the
    // number of bytes the device delivered, but a clean window guards
    // against stale-byte confusion if the device sends a short report.
    for (u32 i = 0; i < HID_BOOT_REPORT_BYTES; i++) {
        iface->int_buf[i] = 0;
    }

    // 1. Enqueue a Normal IN TRB on the int ring.
    u32 enq = iface->int_ring_enqueue_idx;
    u32 ctrl = XHCI_TRB_TYPE(XHCI_TRB_NORMAL) | XHCI_TRB_IOC |
               (iface->int_ring_cycle ? XHCI_TRB_CYCLE : 0);
    u64 trb_phys = iface->int_ring_phys + (u64)enq * XHCI_TRB_BYTES;

    xhci_trb_write(iface->int_ring, enq, (u32)(iface->int_buf_phys & 0xFFFFFFFFu),
                   (u32)(iface->int_buf_phys >> 32),
                   iface->ep_max_packet, // TRB Transfer Length
                   ctrl);

    // Advance + handle Link-TRB wrap.
    enq++;
    if (enq == iface->int_ring_size_trbs - 1u) {
        u32 last = iface->int_ring_size_trbs - 1u;
        xhci_trb_write(iface->int_ring, last, (u32)(iface->int_ring_phys & 0xFFFFFFFFu),
                       (u32)(iface->int_ring_phys >> 32), 0,
                       XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_LINK_TC |
                           (iface->int_ring_cycle ? XHCI_TRB_CYCLE : 0));
        enq = 0;
        iface->int_ring_cycle = !iface->int_ring_cycle;
    }
    iface->int_ring_enqueue_idx = enq;

    // 2. Memory fence + ring slot doorbell at target = EP DCI.
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    xhci_doorbell_ring(c, slot_id, iface->int_ep_dci);

    // 3. Poll the event ring for the matching Transfer Event.
    if (timeout_iters == 0) {
        timeout_iters = 1;
    }
    for (u32 i = 0; i < timeout_iters; i++) {
        u32 idx = c->event_ring_dequeue_idx;
        u32 e_lo, e_hi, e_status, e_control;
        xhci_trb_read(c->event_ring, idx, &e_lo, &e_hi, &e_status, &e_control);

        bool valid = (e_control & XHCI_TRB_CYCLE) != 0;
        if (valid != c->event_ring_cycle) {
            for (u32 j = 0; j < 100u; j++) {
                __asm__ volatile("nop");
            }
            continue;
        }

        // Always advance dequeue + ERDP for any valid event we read.
        u32 next = idx + 1u;
        if (next == c->event_ring_size_trbs) {
            next = 0;
            c->event_ring_cycle = !c->event_ring_cycle;
        }
        c->event_ring_dequeue_idx = next;
        u64 erdp = c->event_ring_phys + (u64)c->event_ring_dequeue_idx * XHCI_TRB_BYTES;
        xhci_rt_write64_pair(c, XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERDP_LO,
                             XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERDP_HI, erdp | (1ull << 3));

        u32 trb_type = (e_control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK;
        if (trb_type != XHCI_TRB_TRANSFER_EVENT) {
            continue;
        }
        u64 ev_trb_ptr = (u64)e_lo | ((u64)e_hi << 32);
        if (ev_trb_ptr != trb_phys) {
            continue;
        }

        u8 cc = (u8)((e_status >> XHCI_CC_SHIFT) & XHCI_CC_MASK);
        u32 residue = e_status & 0xFFFFFFu;
        u32 received = (residue <= iface->ep_max_packet) ? (iface->ep_max_packet - residue) : 0;

        if (cc != XHCI_CC_SUCCESS && cc != XHCI_CC_SHORT_PACKET) {
            LOG_ERROR("xhci", "slot=%u iface[%u] int-IN CC=%u residue=%u", (unsigned)slot_id,
                      (unsigned)iface_idx, (unsigned)cc, (unsigned)residue);
            return CARA_EAGAIN;
        }

        // Cache the bytes back on the interface so the boot decoders
        // (or a future Gleas) can re-read without DMA-buffer races.
        u32 cap = (received < HID_BOOT_REPORT_BYTES) ? received : HID_BOOT_REPORT_BYTES;
        for (u32 k = 0; k < cap; k++) {
            iface->last_report[k] = iface->int_buf[k];
        }
        for (u32 k = cap; k < HID_BOOT_REPORT_BYTES; k++) {
            iface->last_report[k] = 0;
        }
        iface->last_report_bytes = received;
        if (bytes_received_out) {
            *bytes_received_out = received;
        }
        return CARA_EOK;
    }
    return CARA_EAGAIN;
}
