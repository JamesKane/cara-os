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
    u8   ep_num = (u8)(ep_address & 0x0Fu);
    bool is_in  = (ep_address & USB_EP_DIR_IN) != 0;
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
            return 3;          // 1ms ≈ 8 microframes ≈ 2^3
        }
        u32 microframes = (u32)b_interval * 8u;
        u8  log2 = 0;
        while ((1u << log2) < microframes && log2 < 15) {
            log2++;
        }
        return log2;
    }
    default:
        return 3;
    }
}

static int configure_one_int_endpoint(struct XhciController *c,
                                      u8 slot_id, u32 iface_idx)
{
    typeof(&c->slots[slot_id])              slot  = &c->slots[slot_id];
    typeof(&slot->interfaces[iface_idx])    iface = &slot->interfaces[iface_idx];

    if (!iface->valid || !iface->ep_present) {
        return CARA_EINVAL;
    }
    u32 dci = endpoint_dci(iface->ep_address);
    if (dci < 2 || dci > 31) {
        // DCI 0 is the Slot Context, 1 is EP0; legitimate device
        // endpoints occupy 2..31 (xHCI 1.2 §4.5.1).
        LOG_ERROR("xhci",
                  "slot=%u iface[%u] illegal DCI %u for ep_addr=0x%x",
                  (unsigned)slot_id, (unsigned)iface_idx,
                  (unsigned)dci, (unsigned)iface->ep_address);
        return CARA_ERANGE;
    }

    // 1. Allocate the interrupt-IN Transfer Ring + its Link TRB.
    void *ring_kva = nullptr;
    u64 ring_phys = alloc_pages(1, &ring_kva);
    if (!ring_phys) {
        return CARA_ENOMEM;
    }
    iface->int_ring_phys        = ring_phys;
    iface->int_ring             = (volatile u32 *)ring_kva;
    iface->int_ring_size_trbs   = 4096u / 16u;
    iface->int_ring_enqueue_idx = 0;
    iface->int_ring_cycle       = true;
    iface->int_ep_dci           = (u8)dci;
    {
        u32 last = iface->int_ring_size_trbs - 1u;
        xhci_trb_write(iface->int_ring, last,
                       (u32)(ring_phys & 0xFFFFFFFFu),
                       (u32)(ring_phys >> 32),
                       0,
                       XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_LINK_TC);
    }

    // 2. Zero the (reused) Input Context page; the Address Device
    //    fill is stale at this point and the xHC reads any non-zero
    //    bits we've left behind.
    for (u32 i = 0; i < 4096u; i++) {
        slot->input_ctx[i] = 0;
    }

    u32 cb = ctx_bytes(c);
    volatile u32 *icc      = (volatile u32 *)(slot->input_ctx + 0);
    volatile u32 *slot_ctx = (volatile u32 *)(slot->input_ctx + cb);
    volatile u32 *ep_ctx   =
        (volatile u32 *)(slot->input_ctx + (1u + dci) * cb);

    // Input Control Context. Drop=0; Add = A0 (Slot Context) | A_DCI.
    // We need A0 because we're updating the Slot Context's
    // Context Entries field to encompass the new endpoint.
    icc[0] = 0;
    icc[1] = XHCI_INPUT_CTX_ADD_SLOT | (1u << dci);

    // Slot Context. Speed and Root Hub Port Number from the cached
    // post-Address Device state; Context Entries bumped to the highest
    // DCI we're now using. USB Address / Slot State are xHC-controlled
    // (DW3) and software shall write 0 (xHCI 1.2 §6.2.2 / §4.6.6).
    slot_ctx[0] = ((u32)slot->speed << XHCI_SLOT_CTX_DW0_SPEED_SHIFT)
                | ((u32)dci         << XHCI_SLOT_CTX_DW0_CTXENTRIES_SHIFT);
    slot_ctx[1] = ((u32)slot->root_port << XHCI_SLOT_CTX_DW1_RHPORT_SHIFT);

    // EP_DCI Context. INTERRUPT_IN, CErr=3, MaxPacketSize from the
    // descriptor, Interval encoded for the slot's speed.
    u32 mps      = iface->ep_max_packet;
    u8  interval = hid_interval_for_speed(slot->speed, iface->ep_interval);
    ep_ctx[0] = ((u32)interval << XHCI_EP_CTX_DW0_INTERVAL_SHIFT);
    ep_ctx[1] = ((u32)3u << XHCI_EP_CTX_DW1_CERR_SHIFT)
              | ((u32)XHCI_EP_TYPE_INTERRUPT_IN
                  << XHCI_EP_CTX_DW1_EPTYPE_SHIFT)
              | (mps << XHCI_EP_CTX_DW1_MAXPKT_SHIFT);
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
    slot->slot_state = (u8)((dw3 >> XHCI_SLOT_CTX_DW3_STATE_SHIFT)
                            & XHCI_SLOT_CTX_DW3_STATE_MASK);

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
            typeof(&c->slots[sid].interfaces[j]) iface =
                &c->slots[sid].interfaces[j];
            if (!iface->valid || !iface->ep_present) {
                continue;
            }
            if (iface->dispatch != XHCI_HID_KEYBOARD
                && iface->dispatch != XHCI_HID_MOUSE) {
                continue;
            }
            if (iface->ep_xhci_configured) {
                c->n_xhci_configured_interfaces++;
                continue;
            }
            int rc = configure_one_int_endpoint(c, sid, j);
            if (rc != CARA_EOK) {
                LOG_WARN("xhci",
                         "slot=%u iface[%u] Configure Endpoint failed: %d",
                         (unsigned)sid, (unsigned)j, rc);
                continue;
            }
            LOG_INFO("xhci",
                     "slot=%u iface[%u] %s int-IN dci=%u ring=0x%llx slot_state=%u",
                     (unsigned)sid, (unsigned)j,
                     (iface->dispatch == XHCI_HID_KEYBOARD) ? "kbd" : "mouse",
                     (unsigned)iface->int_ep_dci,
                     (u64)iface->int_ring_phys,
                     (unsigned)c->slots[sid].slot_state);
            c->n_xhci_configured_interfaces++;
        }
    }
    LOG_INFO("xhci",
             "%u HID interrupt-IN endpoints xHCI-configured",
             (unsigned)c->n_xhci_configured_interfaces);
    return CARA_EOK;
}
