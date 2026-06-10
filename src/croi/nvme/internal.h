// SPDX-License-Identifier: BSD-2-Clause
//
// Private helpers shared across src/croi/nvme/. Not exported.

#ifndef CROI_NVME_INTERNAL_H
#define CROI_NVME_INTERNAL_H

#include <cara/nvme.h>
#include <cara/types.h>

static inline u32 nvme_read32(const struct NvmeController *c, u32 off)
{
    return *(volatile u32 *)(c->regs + off);
}

static inline void nvme_write32(const struct NvmeController *c, u32 off, u32 v)
{
    *(volatile u32 *)(c->regs + off) = v;
}

// The 64-bit registers (CAP, ASQ, ACQ) tolerate two 32-bit accesses
// (Base §3.1: "64-bit registers ... may be accessed as two 32-bit
// values"); keeping all MMIO 32-bit sidesteps any controller that
// rejects 64-bit cycles, the same posture the xHCI driver takes.
static inline u64 nvme_read64(const struct NvmeController *c, u32 off)
{
    u64 lo = nvme_read32(c, off);
    u64 hi = nvme_read32(c, off + 4);
    return lo | (hi << 32);
}

static inline void nvme_write64(const struct NvmeController *c, u32 off, u64 v)
{
    nvme_write32(c, off, (u32)(v & 0xFFFFFFFFu));
    nvme_write32(c, off + 4, (u32)(v >> 32));
}

// SQ y tail / CQ y head doorbells (Base §3.1.16).
static inline void nvme_ring_sq_tail(const struct NvmeController *c, u16 qid, u16 tail)
{
    u32 off = NVME_DOORBELL_BASE + (2u * qid) * (4u << c->dstrd);
    nvme_write32(c, off, tail);
}

static inline void nvme_ring_cq_head(const struct NvmeController *c, u16 qid, u16 head)
{
    u32 off = NVME_DOORBELL_BASE + (2u * qid + 1u) * (4u << c->dstrd);
    nvme_write32(c, off, head);
}

// Spin up to ~1M iterations waiting for `(reg & mask) == expect` on a
// 32-bit controller register. Same shape (and the same real-time
// envelope) as xhci_spin_for_mask.
[[nodiscard]] static inline bool nvme_spin_for_mask(const struct NvmeController *c, u32 off,
                                                    u32 mask, u32 expect)
{
    for (u32 i = 0; i < 1000000u; i++) {
        if ((nvme_read32(c, off) & mask) == expect) {
            return true;
        }
        for (u32 j = 0; j < 100u; j++) {
            __asm__ volatile("nop");
        }
    }
    return false;
}

// Submit one command on `q` and poll its CQ for the matching
// completion. Shared by the admin path (Croi_Nvme_AdminCmd) and the
// I/O path (Croi_Nvme_Read/Write). Returns CARA_EOK / CARA_EIO /
// CARA_EAGAIN as documented on Croi_Nvme_AdminCmd.
[[nodiscard]] int nvme_submit_sync(struct NvmeController *c, struct NvmeQueue *q, u8 opcode,
                                   u32 nsid, u64 prp1, u64 prp2, u32 cdw10, u32 cdw11, u32 cdw12,
                                   u32 *cqe_dw0_out);

#endif
