// SPDX-License-Identifier: BSD-2-Clause
//
// NVMe I/O queue creation (N4) + synchronous block read/write (N5).
// Base §5.4/§5.5 (Create I/O CQ/SQ); NVM Command Set §3.2 (Read),
// §3.3 (Write); Base §4.1.2 (PRP data transfers).

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/nvme.h>
#include <cara/types.h>

#include "internal.h"

extern struct PageAllocator g_page_alloc;

[[nodiscard]] int Croi_Nvme_CreateIoQueues(struct NvmeController *c)
{
    if (!c || !c->enabled) {
        return CARA_EINVAL;
    }
    if (c->io_ready) {
        return CARA_EOK;
    }

    struct NvmeQueue *q = &c->io;
    *q = (struct NvmeQueue){ 0 };
    q->qid = 1;
    q->entries = CARA_NVME_QUEUE_ENTRIES;
    q->cq_phase = true;

    u64 sq_phys = Page_Alloc(&g_page_alloc, 1);
    u64 cq_phys = Page_Alloc(&g_page_alloc, 1);
    if (sq_phys == 0 || cq_phys == 0) {
        LOG_ERROR("nvme", "I/O queue page allocation failed");
        return CARA_ENOMEM;
    }
    q->sq_phys = sq_phys;
    q->sq = (volatile u32 *)Mm_PhysToVirt(sq_phys);
    q->cq_phys = cq_phys;
    q->cq = (volatile u32 *)Mm_PhysToVirt(cq_phys);

    // CQ first — the SQ's create command names its CQID (Base §5.4:
    // the completion queue must exist before any SQ binds to it).
    u32 cdw10 = ((q->entries - 1) << NVME_CREATEQ_QSIZE_SHIFT) | q->qid;
    int rc = Croi_Nvme_AdminCmd(c, NVME_ADMIN_CREATE_IO_CQ, 0, q->cq_phys, 0, cdw10,
                                NVME_CREATEQ_PC, nullptr); // IEN=0: polled
    if (rc != CARA_EOK) {
        LOG_ERROR("nvme", "Create I/O CQ failed: %d", rc);
        return rc;
    }

    u32 cdw11 = ((u32)q->qid << NVME_CREATESQ_CQID_SHIFT) | NVME_CREATEQ_PC;
    rc = Croi_Nvme_AdminCmd(c, NVME_ADMIN_CREATE_IO_SQ, 0, q->sq_phys, 0, cdw10, cdw11, nullptr);
    if (rc != CARA_EOK) {
        LOG_ERROR("nvme", "Create I/O SQ failed: %d", rc);
        return rc;
    }

    c->io_ready = true;
    LOG_INFO("nvme", "I/O queue pair %u live (%u entries, polled)", (unsigned)q->qid,
             (unsigned)q->entries);
    return CARA_EOK;
}

// Shared argument validation + PRP construction for Read/Write. A
// transfer spans at most two pages (PRP1 + PRP2); PRP lists are a
// follow-on epic when CaraFS wants bigger I/O.
static int nvme_io_cmd(struct NvmeController *c, u8 opcode, u64 lba, u32 n_blocks, const void *buf)
{
    if (!c || !c->io_ready || !c->ns.valid || !buf || n_blocks == 0) {
        return CARA_EINVAL;
    }
    u64 bytes = (u64)n_blocks * c->ns.block_bytes;
    u64 phys = Mm_VirtToPhys(buf);
    if ((phys & (CARA_PAGE_SIZE - 1)) != 0 || bytes > 2 * CARA_PAGE_SIZE) {
        return CARA_EINVAL;
    }
    if (lba + n_blocks < lba || lba + n_blocks > c->ns.n_blocks) {
        return CARA_ERANGE;
    }

    u64 prp2 = (bytes > CARA_PAGE_SIZE) ? phys + CARA_PAGE_SIZE : 0;

    // CDW10/11 = starting LBA; CDW12[15:0] = 0-based block count.
    return nvme_submit_sync(c, &c->io, opcode, CARA_NVME_NSID, phys, prp2, (u32)(lba & 0xFFFFFFFFu),
                            (u32)(lba >> 32), n_blocks - 1, nullptr);
}

[[nodiscard]] int Croi_Nvme_Read(struct NvmeController *c, u64 lba, u32 n_blocks, void *buf)
{
    return nvme_io_cmd(c, NVME_NVM_READ, lba, n_blocks, buf);
}

[[nodiscard]] int Croi_Nvme_Write(struct NvmeController *c, u64 lba, u32 n_blocks, const void *buf)
{
    return nvme_io_cmd(c, NVME_NVM_WRITE, lba, n_blocks, buf);
}

[[nodiscard]] int Croi_Nvme_Flush(struct NvmeController *c)
{
    if (!c || !c->io_ready) {
        return CARA_EINVAL;
    }
    // Flush carries no data transfer: PRP1/PRP2 and all CDWs are zero
    // (NVM Command Set §3.1). NSID 1 flushes the one namespace we drive.
    return nvme_submit_sync(c, &c->io, NVME_NVM_FLUSH, CARA_NVME_NSID, 0, 0, 0, 0, 0, nullptr);
}
