// SPDX-License-Identifier: BSD-2-Clause
//
// xHCI USB control transfers over EP0 (Default Control Pipe) — UC.1.
// Implements the xHCI 1.2 §4.11.2.2 Setup / Data / Status TRB chain
// for standard USB §9.3 control transfers, plus a wrapper for the
// 18-byte GET_DESCRIPTOR(DEVICE) read every USB device must satisfy.
//
// Phase 1 polls the event ring after ringing the slot doorbell —
// interrupts wait for UB.7 done-for-real. The poll loop here mirrors
// the one in xhci_cmd_send (slots.c); when UB.7 lands we'll fold both
// onto a shared interrupt-driven event dispatcher.

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/types.h>
#include <cara/usb.h>
#include <cara/xhci.h>

#include "internal.h"

// Enqueue one TRB onto a slot's EP0 transfer ring, advancing the
// per-slot enqueue index and PCS, handling Link TRB wrap. Returns the
// physical address of the TRB just written (callers use it to match
// the corresponding Transfer Event).
static u64 ep0_ring_enqueue(struct XhciController *c, u8 slot_id,
                            u32 p_lo, u32 p_hi, u32 status,
                            u32 control_no_cycle)
{
    auto slot = &c->slots[slot_id];

    u32 enq  = slot->ep0_ring_enqueue_idx;
    u32 ctrl = control_no_cycle
             | (slot->ep0_ring_cycle ? XHCI_TRB_CYCLE : 0);
    u64 trb_phys = slot->ep0_ring_phys + (u64)enq * XHCI_TRB_BYTES;

    xhci_trb_write(slot->ep0_ring, enq, p_lo, p_hi, status, ctrl);

    enq++;
    if (enq == slot->ep0_ring_size_trbs - 1u) {
        // Refresh the Link TRB's cycle bit so the consumer accepts it,
        // then toggle PCS (xHCI 1.2 §4.9.2). The Link TRB itself was
        // installed at slot allocation; we only update its cycle here.
        u32 last = slot->ep0_ring_size_trbs - 1u;
        xhci_trb_write(slot->ep0_ring, last,
                       (u32)(slot->ep0_ring_phys & 0xFFFFFFFFu),
                       (u32)(slot->ep0_ring_phys >> 32),
                       0,
                       XHCI_TRB_TYPE(XHCI_TRB_LINK)
                           | XHCI_TRB_LINK_TC
                           | (slot->ep0_ring_cycle ? XHCI_TRB_CYCLE : 0));
        enq = 0;
        slot->ep0_ring_cycle = !slot->ep0_ring_cycle;
    }
    slot->ep0_ring_enqueue_idx = enq;
    return trb_phys;
}

// Poll the event ring until a Transfer Event arrives for the TRB whose
// physical address is `expected_trb_phys`. Returns the Completion Code
// (XHCI_CC_*); 0 on timeout. `*residue_out` receives the residue
// length (Transfer Event status[23:0]).
static u8 wait_transfer_event(struct XhciController *c,
                              u64 expected_trb_phys,
                              u32 *residue_out)
{
    if (residue_out) {
        *residue_out = 0;
    }
    for (u32 i = 0; i < 1000000u; i++) {
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

        u32 next = idx + 1u;
        if (next == c->event_ring_size_trbs) {
            next = 0;
            c->event_ring_cycle = !c->event_ring_cycle;
        }
        c->event_ring_dequeue_idx = next;

        u64 erdp = c->event_ring_phys
                 + (u64)c->event_ring_dequeue_idx * XHCI_TRB_BYTES;
        xhci_rt_write64_pair(c,
                             XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERDP_LO,
                             XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERDP_HI,
                             erdp | (1ull << 3));

        u32 trb_type = (e_control >> XHCI_TRB_TYPE_SHIFT)
                       & XHCI_TRB_TYPE_MASK;
        if (trb_type == XHCI_TRB_TRANSFER_EVENT) {
            u64 ev_trb_ptr = (u64)e_lo | ((u64)e_hi << 32);
            if (ev_trb_ptr != expected_trb_phys) {
                continue;
            }
            if (residue_out) {
                *residue_out = e_status & 0xFFFFFFu;
            }
            return (u8)((e_status >> XHCI_CC_SHIFT) & XHCI_CC_MASK);
        }
        // Other event types (e.g. residual Port Status Change) are
        // observed but not consumed beyond ERDP advancement.
    }
    return 0;
}

[[nodiscard]] int Croi_Xhci_ControlTransfer(struct XhciController *c,
                                            u8 slot_id,
                                            const struct UsbSetupPacket *setup,
                                            void *buf, u32 buf_len,
                                            u32 *residue_out)
{
    if (!c || !c->running || !setup
        || slot_id == 0 || slot_id > CARA_XHCI_MAX_SLOTS) {
        return CARA_EINVAL;
    }
    if (!c->slots[slot_id].in_use) {
        return CARA_EINVAL;
    }
    if (buf_len > 0 && !buf) {
        return CARA_EINVAL;
    }

    bool dir_in   = (setup->bmRequestType & USB_DIR_DEVICE_TO_HOST) != 0;
    bool has_data = buf_len > 0;
    u32  trt;
    if (!has_data) {
        trt = XHCI_TRB_TRT_NO_DATA;
    } else if (dir_in) {
        trt = XHCI_TRB_TRT_IN_DATA;
    } else {
        trt = XHCI_TRB_TRT_OUT_DATA;
    }

    // 1. Setup Stage TRB. The 8-byte SETUP packet rides in DW0/DW1 of
    //    the TRB itself (IDT = 1).
    u32 setup_lo = (u32)setup->bmRequestType
                 | ((u32)setup->bRequest << 8)
                 | ((u32)setup->wValue   << 16);
    u32 setup_hi = (u32)setup->wIndex
                 | ((u32)setup->wLength  << 16);
    (void)ep0_ring_enqueue(c, slot_id, setup_lo, setup_hi,
                           8u,                              // TRB Transfer Length
                           XHCI_TRB_TYPE(XHCI_TRB_SETUP_STAGE)
                               | XHCI_TRB_IDT
                               | trt);

    // 2. Data Stage TRB (only if there's a buffer). For multi-packet
    //    transfers, a single TRB suffices when buf_len fits in one TD;
    //    Phase 1 GET_DESCRIPTOR reads stay well under 64 KiB so we
    //    don't split.
    if (has_data) {
        u64 buf_phys = Mm_VirtToPhys(buf);
        (void)ep0_ring_enqueue(c, slot_id,
                               (u32)(buf_phys & 0xFFFFFFFFu),
                               (u32)(buf_phys >> 32),
                               buf_len,
                               XHCI_TRB_TYPE(XHCI_TRB_DATA_STAGE)
                                   | (dir_in ? XHCI_TRB_DIR_IN : 0));
    }

    // 3. Status Stage TRB. Direction is the opposite of the Data Stage:
    //    for an IN data transfer, the status stage is OUT (DIR=0); for
    //    OUT or no-data, the status stage is IN (DIR=1). IOC is set so
    //    the controller posts a Transfer Event when the whole TD is
    //    done. xHCI 1.2 §4.11.2.2 / §6.4.1.2.3.
    bool status_dir_in = !dir_in || !has_data;
    u64 status_trb_phys = ep0_ring_enqueue(
        c, slot_id, 0, 0, 0,
        XHCI_TRB_TYPE(XHCI_TRB_STATUS_STAGE)
            | XHCI_TRB_IOC
            | (status_dir_in ? XHCI_TRB_DIR_IN : 0));

    // 4. Make the TD globally visible before ringing the slot doorbell
    //    with target = 1 (DCI for EP0).
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    xhci_doorbell_ring(c, slot_id, 1);

    // 5. Wait for the Transfer Event matching the Status TRB.
    u32 residue = 0;
    u8  cc      = wait_transfer_event(c, status_trb_phys, &residue);
    if (residue_out) {
        *residue_out = residue;
    }
    if (cc != XHCI_CC_SUCCESS) {
        LOG_ERROR("xhci",
                  "control xfer slot=%u request=0x%x CC=%u residue=%u",
                  (unsigned)slot_id,
                  (unsigned)setup->bRequest,
                  (unsigned)cc, (unsigned)residue);
        return CARA_EAGAIN;
    }
    return CARA_EOK;
}

[[nodiscard]] int Croi_Xhci_GetDeviceDescriptor(struct XhciController *c,
                                                u8 slot_id,
                                                struct UsbDeviceDescriptor *out)
{
    if (!c || slot_id == 0 || slot_id > CARA_XHCI_MAX_SLOTS) {
        return CARA_EINVAL;
    }
    if (!c->slots[slot_id].in_use || !c->slots[slot_id].dma_buf) {
        return CARA_EINVAL;
    }

    auto slot = &c->slots[slot_id];

    // Zero the scratch region we'll DMA into; the controller writes
    // exactly bLength bytes (18) on success but we want a clean copy.
    for (u32 i = 0; i < USB_DEVICE_DESCRIPTOR_BYTES; i++) {
        slot->dma_buf[i] = 0;
    }

    struct UsbSetupPacket setup = {
        .bmRequestType = USB_DIR_DEVICE_TO_HOST | USB_TYPE_STANDARD
                       | USB_RECIP_DEVICE,
        .bRequest      = USB_REQ_GET_DESCRIPTOR,
        .wValue        = (u16)((USB_DT_DEVICE << 8) | 0),
        .wIndex        = 0,
        .wLength       = USB_DEVICE_DESCRIPTOR_BYTES,
    };

    u32 residue = 0;
    // Discard volatile for the buf pointer: ControlTransfer only uses it
    // to derive the physical address via Mm_VirtToPhys. The DMA writes
    // happen behind our back; we read back through slot->dma_buf which
    // keeps the volatile qualifier.
    int rc = Croi_Xhci_ControlTransfer(c, slot_id, &setup,
                                       (void *)(uptr)slot->dma_buf,
                                       USB_DEVICE_DESCRIPTOR_BYTES,
                                       &residue);
    if (rc != CARA_EOK) {
        return rc;
    }
    u32 received = USB_DEVICE_DESCRIPTOR_BYTES - residue;
    if (received < USB_DEVICE_DESCRIPTOR_BYTES) {
        LOG_ERROR("xhci",
                  "slot=%u short device descriptor: got %u of %u bytes",
                  (unsigned)slot_id, (unsigned)received,
                  (unsigned)USB_DEVICE_DESCRIPTOR_BYTES);
        return CARA_EAGAIN;
    }

    // Cache the raw bytes in the slot, then copy out to caller.
    for (u32 i = 0; i < USB_DEVICE_DESCRIPTOR_BYTES; i++) {
        slot->device_descriptor.raw[i] = slot->dma_buf[i];
    }
    slot->device_descriptor.valid = true;

    if (out) {
        for (u32 i = 0; i < sizeof(*out); i++) {
            ((u8 *)out)[i] = slot->device_descriptor.raw[i];
        }
    }
    return CARA_EOK;
}

[[nodiscard]] int Croi_Xhci_ReadDescriptors(struct XhciController *c)
{
    if (!c || !c->running) {
        return CARA_EINVAL;
    }

    c->n_described_slots = 0;
    for (u8 sid = 1; sid <= CARA_XHCI_MAX_SLOTS; sid++) {
        if (!c->slots[sid].in_use) {
            continue;
        }
        if (c->slots[sid].device_descriptor.valid) {
            c->n_described_slots++;
            continue;
        }
        int rc = Croi_Xhci_GetDeviceDescriptor(c, sid, nullptr);
        if (rc != CARA_EOK) {
            LOG_WARN("xhci",
                     "slot=%u GetDeviceDescriptor failed: %d", sid, rc);
            continue;
        }
        const struct UsbDeviceDescriptor *d =
            (const struct UsbDeviceDescriptor *)
                c->slots[sid].device_descriptor.raw;
        LOG_INFO("xhci",
                 "slot=%u device-desc: bcdUSB=0x%x class=%u/%u/%u VID:PID=0x%x:0x%x cfgs=%u",
                 (unsigned)sid,
                 (unsigned)d->bcdUSB,
                 (unsigned)d->bDeviceClass,
                 (unsigned)d->bDeviceSubClass,
                 (unsigned)d->bDeviceProtocol,
                 (unsigned)d->idVendor,
                 (unsigned)d->idProduct,
                 (unsigned)d->bNumConfigurations);
        c->n_described_slots++;
    }
    LOG_INFO("xhci", "%u of %u addressed slots described",
             (unsigned)c->n_described_slots,
             (unsigned)c->n_addressed_slots);
    return CARA_EOK;
}
