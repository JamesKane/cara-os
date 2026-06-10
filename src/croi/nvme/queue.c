// SPDX-License-Identifier: BSD-2-Clause
//
// NVMe admin queue bring-up + the shared synchronous submit/poll
// primitive (N2). Base §3.1.5 (CC programming), §4.1 (queue
// mechanics), §7.2.1 (command processing model).

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/nvme.h>
#include <cara/types.h>

#include "internal.h"

extern struct PageAllocator g_page_alloc;

// Allocate `n_pages` zeroed, page-aligned pages and hand back both
// views. Same helper shape as the xHCI setup path.
static bool alloc_pages(u32 n_pages, u64 *phys_out, volatile void **kva_out)
{
    u64 phys = Page_Alloc(&g_page_alloc, n_pages);
    if (phys == 0) {
        return false;
    }
    *phys_out = phys;
    *kva_out = (volatile void *)Mm_PhysToVirt(phys);
    return true;
}

[[nodiscard]] int Croi_Nvme_Setup(struct NvmeController *c)
{
    if (!c || !c->regs) {
        return CARA_EINVAL;
    }
    if (c->enabled) {
        return CARA_EOK;
    }

    // One page per queue: 64 SQEs × 64 B = 4096, 64 CQEs × 16 B = 1024
    // (CQ rounds up to a page anyway). Page_Alloc zeroes, which also
    // zeroes every CQE Phase Tag — mandatory, since the controller's
    // first pass writes Phase = 1 (Base §4.1.1).
    struct NvmeQueue *q = &c->admin;
    *q = (struct NvmeQueue){ 0 };
    q->qid = 0;
    q->entries = CARA_NVME_QUEUE_ENTRIES;
    q->cq_phase = true;
    if (!alloc_pages(1, &q->sq_phys, (volatile void **)&q->sq) ||
        !alloc_pages(1, &q->cq_phys, (volatile void **)&q->cq) ||
        !alloc_pages(1, &c->dma_buf_phys, (volatile void **)&c->dma_buf)) {
        LOG_ERROR("nvme", "admin queue page allocation failed");
        return CARA_ENOMEM;
    }

    // AQA holds 0-based sizes for both admin queues (Base §3.1.8);
    // ASQ/ACQ take the page-aligned physical bases.
    u32 aqa = ((q->entries - 1) << NVME_AQA_ACQS_SHIFT) | ((q->entries - 1) << NVME_AQA_ASQS_SHIFT);
    nvme_write32(c, NVME_REG_AQA, aqa);
    nvme_write64(c, NVME_REG_ASQ, q->sq_phys);
    nvme_write64(c, NVME_REG_ACQ, q->cq_phys);

    // CC: NVM command set, 4 KiB MPS, round-robin arbitration, entry
    // sizes 2^6 / 2^4, then EN. The entry-size fields must be valid
    // before or with EN=1 (Base §3.1.5).
    u32 cc = NVME_CC_CSS_NVM | (0u << NVME_CC_MPS_SHIFT) | NVME_CC_AMS_RR | NVME_CC_SHN_NONE |
             (NVME_SQE_SHIFT << NVME_CC_IOSQES_SHIFT) | (NVME_CQE_SHIFT << NVME_CC_IOCQES_SHIFT);
    nvme_write32(c, NVME_REG_CC, cc);
    nvme_write32(c, NVME_REG_CC, cc | NVME_CC_EN);

    if (!nvme_spin_for_mask(c, NVME_REG_CSTS, NVME_CSTS_RDY, NVME_CSTS_RDY)) {
        LOG_ERROR("nvme", "CSTS.RDY did not assert after CC.EN=1 (CSTS=0x%x)",
                  nvme_read32(c, NVME_REG_CSTS));
        return CARA_EAGAIN;
    }
    if (nvme_read32(c, NVME_REG_CSTS) & NVME_CSTS_CFS) {
        LOG_ERROR("nvme", "controller fatal status set during enable");
        return CARA_EIO;
    }

    c->enabled = true;
    LOG_INFO("nvme", "enabled; admin queues live (asq=0x%llx acq=0x%llx %u entries)",
             (u64)q->sq_phys, (u64)q->cq_phys, (unsigned)q->entries);
    return CARA_EOK;
}

// ---- Synchronous submit + completion poll ----------------------------------

[[nodiscard]] int nvme_submit_sync(struct NvmeController *c, struct NvmeQueue *q, u8 opcode,
                                   u32 nsid, u64 prp1, u64 prp2, u32 cdw10, u32 cdw11, u32 cdw12,
                                   u32 *cqe_dw0_out)
{
    u16 cid = q->next_cid++;

    // Build the 64-byte SQE in place (Base §4.2). The queue memory is
    // plain cacheable RAM the controller DMAs from; write the words,
    // fence, then ring the doorbell so the device never races a
    // half-written entry.
    volatile u32 *sqe = &q->sq[(u32)q->sq_tail * NVME_SQE_DWORDS];
    for (u32 i = 0; i < NVME_SQE_DWORDS; i++) {
        sqe[i] = 0;
    }
    sqe[0] = (u32)opcode | ((u32)cid << 16); // CDW0: OPC | CID; FUSE/PSDT = 0 (PRPs)
    sqe[1] = nsid;
    sqe[6] = (u32)(prp1 & 0xFFFFFFFFu);
    sqe[7] = (u32)(prp1 >> 32);
    sqe[8] = (u32)(prp2 & 0xFFFFFFFFu);
    sqe[9] = (u32)(prp2 >> 32);
    sqe[10] = cdw10;
    sqe[11] = cdw11;
    sqe[12] = cdw12;

    q->sq_tail = (u16)((q->sq_tail + 1) % q->entries);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    nvme_ring_sq_tail(c, q->qid, q->sq_tail);

    // Poll the CQ head for a phase-matched entry (Base §4.1.1: the
    // Phase Tag inverts each wrap, so "new" is whatever q->cq_phase
    // currently expects). The polled Phase 2 driver submits one
    // command at a time, so the next new CQE is ours — but verify CID
    // anyway and fail loudly on a mismatch.
    for (u32 spin = 0; spin < 1000000u; spin++) {
        volatile u32 *cqe = &q->cq[(u32)q->cq_head * NVME_CQE_DWORDS];
        u32 dw3 = cqe[3];
        bool phase = (dw3 & NVME_CQE_DW3_PHASE) != 0;
        if (phase == q->cq_phase) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            u32 dw0 = cqe[0];
            u16 got_cid = (u16)(dw3 & NVME_CQE_DW3_CID_MASK);
            u32 status = dw3 >> NVME_CQE_DW3_STATUS_SHIFT;

            q->cq_head = (u16)((q->cq_head + 1) % q->entries);
            if (q->cq_head == 0) {
                q->cq_phase = !q->cq_phase;
            }
            nvme_ring_cq_head(c, q->qid, q->cq_head);

            if (got_cid != cid) {
                LOG_ERROR("nvme", "qid %u completion CID %u != submitted %u", (unsigned)q->qid,
                          (unsigned)got_cid, (unsigned)cid);
                return CARA_EIO;
            }
            if (status != 0) {
                // Status Field: SCT in bits [10:8], SC in [7:0] of the
                // 15-bit field (Base §4.6.1).
                LOG_ERROR("nvme", "opc 0x%x failed: sct=%u sc=0x%x", (unsigned)opcode,
                          (unsigned)((status >> 8) & 0x7u), (unsigned)(status & 0xFFu));
                return CARA_EIO;
            }
            if (cqe_dw0_out) {
                *cqe_dw0_out = dw0;
            }
            return CARA_EOK;
        }
        for (u32 j = 0; j < 100u; j++) {
            __asm__ volatile("nop");
        }
    }

    LOG_ERROR("nvme", "opc 0x%x timed out on qid %u", (unsigned)opcode, (unsigned)q->qid);
    return CARA_EAGAIN;
}

[[nodiscard]] int Croi_Nvme_AdminCmd(struct NvmeController *c, u8 opcode, u32 nsid, u64 prp1,
                                     u64 prp2, u32 cdw10, u32 cdw11, u32 *cqe_dw0_out)
{
    if (!c || !c->enabled) {
        return CARA_EINVAL;
    }
    return nvme_submit_sync(c, &c->admin, opcode, nsid, prp1, prp2, cdw10, cdw11, 0, cqe_dw0_out);
}
