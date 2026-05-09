// SPDX-License-Identifier: BSD-2-Clause
//
// xHCI controller setup — DCBAA + Command Ring + Event Ring + the
// Run/Stop transition. Runs after Croi_Xhci_Probe has reset the
// controller. xHCI 1.2 §4.2 is the source for the sequence.
//
// Memory layout: every DMA structure is page-allocated, naturally
// page-aligned (which satisfies xHCI's 64-byte alignment for DCBAA,
// CRCR, ERST, and ERDP). Page_Alloc zeroes pages before return, so
// the empty Command Ring and Event Ring segments come up with all
// TRBs at type 0 (Reserved, ignored by the controller).
//
// Cache coherency: this file emits __atomic_thread_fence between
// memory-resident structure writes and the MMIO writes that hand
// the addresses to the controller. On QEMU virt the emulated MMU
// is coherent so that's enough; on real hardware (X1) we'll need
// cbo.flush via the Zicbom extension before publishing addresses
// the device DMAs from. Phase 5 handles that.

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/types.h>
#include <cara/xhci.h>

#include "internal.h"

extern struct PageAllocator g_page_alloc;

// Allocate `n_pages` contiguous pages from the kernel page allocator.
// Returns the CPU physical address (== PCI bus address under the
// 1:1 ranges QEMU virt and the X1 both ship); writes the upper-half
// kernel-VA pointer to *kva_out so callers can poke fields via the
// direct map.
static u64 alloc_pages(u32 n_pages, void **kva_out)
{
    u64 phys = Page_Alloc(&g_page_alloc, n_pages);
    if (kva_out) {
        *kva_out = phys ? Mm_PhysToVirt(phys) : nullptr;
    }
    return phys;
}

[[nodiscard]] int Croi_Xhci_Setup(struct XhciController *c)
{
    if (!c || !c->cap_regs || !c->op_regs) {
        return CARA_EINVAL;
    }
    // Reset path leaves USBSTS.HCH set (controller halted). If we got
    // called twice, or the host already started the controller from
    // somewhere else, refuse rather than corrupt running state.
    u32 sts = xhci_op_read32(c, XHCI_OP_USBSTS);
    if ((sts & XHCI_USBSTS_HCH) == 0) {
        LOG_ERROR("xhci", "Setup called while controller running (USBSTS=0x%x)", (unsigned)sts);
        return CARA_EINVAL;
    }
    // Sanity: each xHCI page must be 4 KiB for our Page_Alloc(1) to
    // satisfy "one xHCI page". If a controller advertises larger
    // (unusual; QEMU and the X1 are both 4 KiB), we'd need
    // multi-page contiguous allocations for scratchpad bufs.
    if (c->page_size_bytes != 4096) {
        LOG_ERROR("xhci", "PAGESIZE=%u; Phase 1 only handles 4 KiB", (unsigned)c->page_size_bytes);
        return CARA_ERANGE;
    }

    // 1. CONFIG.MaxSlotsEn = max_slots.
    xhci_op_write32(c, XHCI_OP_CONFIG, c->max_slots & XHCI_CONFIG_MAXSLOTSEN_MASK);

    // 2. DCBAA. (max_slots + 1) entries × 8 bytes ≤ 4096; one page.
    void *kva = nullptr;
    c->dcbaa_phys = alloc_pages(1, &kva);
    if (!c->dcbaa_phys) {
        return CARA_ENOMEM;
    }
    c->dcbaa = (volatile u64 *)kva;

    // 3. Scratchpad if the controller asks for any.
    if (c->max_scratchpad_buffers > 0) {
        if (c->max_scratchpad_buffers > 64) {
            LOG_ERROR("xhci", "%u scratchpad bufs > 64 (Phase 1 limit)",
                      (unsigned)c->max_scratchpad_buffers);
            return CARA_ERANGE;
        }
        c->scratchpad_array_phys = alloc_pages(1, &kva);
        if (!c->scratchpad_array_phys) {
            return CARA_ENOMEM;
        }
        c->scratchpad_array = (volatile u64 *)kva;
        for (u32 i = 0; i < c->max_scratchpad_buffers; i++) {
            u64 buf_phys = alloc_pages(1, nullptr);
            if (!buf_phys) {
                return CARA_ENOMEM;
            }
            c->scratchpad_buf_phys[i] = buf_phys;
            c->scratchpad_array[i] = buf_phys;
        }
        c->n_scratchpad_bufs = c->max_scratchpad_buffers;
        c->dcbaa[0] = c->scratchpad_array_phys;
    } else {
        c->dcbaa[0] = 0;
        c->n_scratchpad_bufs = 0;
    }

    // Make sure the DCBAA writes are visible before the controller
    // reads them via DCBAAP. (QEMU's emulated MMU is coherent, but
    // the fence orders w.r.t. the upcoming MMIO write and matters
    // on real silicon.)
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    // 4. DCBAAP. xHCI 1.2 §5.4.6: low 6 bits reserved, page-aligned
    //    address satisfies that.
    xhci_op_write64_pair(c, XHCI_OP_DCBAAP_LO, XHCI_OP_DCBAAP_HI, c->dcbaa_phys);

    // 5. Command Ring: one page = 256 TRBs. All zeroed by Page_Alloc.
    c->cmd_ring_phys = alloc_pages(1, &kva);
    if (!c->cmd_ring_phys) {
        return CARA_ENOMEM;
    }
    c->cmd_ring = (volatile u32 *)kva;
    c->cmd_ring_size_trbs = 4096u / 16u;
    c->cmd_ring_enqueue_idx = 0;
    c->cmd_ring_cycle = true; // initial RCS = 1

    // Install a Link TRB at the last slot pointing back to index 0,
    // with TC = 1 so the consumer toggles cycle on wrap. Cycle bit on
    // the Link TRB starts at 0 (the producer's "stale" cycle); the
    // first wrap will flip it to 1 and toggle PCS to 0. xHCI 1.2 §4.9.2.
    {
        u32 last = c->cmd_ring_size_trbs - 1;
        xhci_trb_write(c->cmd_ring, last, (u32)(c->cmd_ring_phys & 0xFFFFFFFFu),
                       (u32)(c->cmd_ring_phys >> 32), 0,
                       XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_LINK_TC);
    }

    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    // 6. CRCR. xHCI 1.2 §5.4.5: bit 0 = RCS, bits 1-5 = control,
    //    bits 6-63 = Command Ring Pointer. Order matters: write LO first
    //    (carrying the page-aligned address low bits + RCS), then HI.
    //    QEMU's qemu-xhci latches LO on its way through and runs the
    //    ring-init logic on the HI write using the latched LO; reversing
    //    the order points the controller's command-ring dequeue at zero
    //    and the doorbell silently no-ops.
    xhci_op_write32(c, XHCI_OP_CRCR_LO, (u32)(c->cmd_ring_phys & 0xFFFFFFC0u) | XHCI_CRCR_RCS);
    xhci_op_write32(c, XHCI_OP_CRCR_HI, (u32)(c->cmd_ring_phys >> 32));

    // 7. ERST + Event Ring Segment 0. ERST is one page; ERST entry
    //    is 16 bytes (low u64 = ring seg address, high u64 = size in
    //    low 16 bits + reserved in high 48). Ring segment is one page.
    c->erst_phys = alloc_pages(1, &kva);
    if (!c->erst_phys) {
        return CARA_ENOMEM;
    }
    c->erst = (volatile u64 *)kva;
    c->event_ring_phys = alloc_pages(1, &kva);
    if (!c->event_ring_phys) {
        return CARA_ENOMEM;
    }
    c->event_ring = (volatile u32 *)kva;
    c->event_ring_size_trbs = 4096u / 16u;
    c->event_ring_dequeue_idx = 0;
    c->event_ring_cycle = true;

    c->erst[0] = c->event_ring_phys;
    c->erst[1] = (u64)c->event_ring_size_trbs; // size in low 16, rest 0

    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    // 8. Interrupter 0: ERSTSZ → ERDP → ERSTBA. Writing ERSTBA last
    //    triggers the controller to load ERST. xHCI 1.2 §5.5.2.3.
    xhci_rt_write32(c, XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERSTSZ, 1);
    xhci_rt_write64_pair(c, XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERDP_LO,
                         XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERDP_HI, c->event_ring_phys);
    xhci_rt_write64_pair(c, XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERSTBA_LO,
                         XHCI_RT_INTR0_BASE + XHCI_RT_INTR_ERSTBA_HI, c->erst_phys);
    // IMAN: leave IE = 0 (no interrupts wired in this milestone).
    xhci_rt_write32(c, XHCI_RT_INTR0_BASE + XHCI_RT_INTR_IMAN, 0);

    // 9. Run/Stop = 1.
    u32 cmd = xhci_op_read32(c, XHCI_OP_USBCMD);
    xhci_op_write32(c, XHCI_OP_USBCMD, cmd | XHCI_USBCMD_RUN);

    // 10. Wait USBSTS.HCH = 0 (controller running).
    if (!xhci_spin_for_mask((volatile u32 *)(c->op_regs + XHCI_OP_USBSTS), XHCI_USBSTS_HCH, 0)) {
        LOG_ERROR("xhci", "Run/Stop transition failed; USBSTS=0x%x",
                  xhci_op_read32(c, XHCI_OP_USBSTS));
        return CARA_EAGAIN;
    }

    c->running = true;
    LOG_INFO("xhci", "running. DCBAA=0x%llx CmdRing=0x%llx EventRing=0x%llx ERST=0x%llx",
             (u64)c->dcbaa_phys, (u64)c->cmd_ring_phys, (u64)c->event_ring_phys, (u64)c->erst_phys);
    if (c->n_scratchpad_bufs > 0) {
        LOG_INFO("xhci", "scratchpad: array=0x%llx (%u bufs of %u bytes each)",
                 (u64)c->scratchpad_array_phys, (unsigned)c->n_scratchpad_bufs,
                 (unsigned)c->page_size_bytes);
    }
    return CARA_EOK;
}
