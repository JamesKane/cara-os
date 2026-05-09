// SPDX-License-Identifier: BSD-2-Clause
//
// Private helpers shared between init.c and setup.c. Not exported
// outside src/croi/xhci/.

#ifndef CROI_XHCI_INTERNAL_H
#define CROI_XHCI_INTERNAL_H

#include <cara/types.h>
#include <cara/xhci.h>

// xHCI registers accept 32-bit accesses only on most controllers
// (qemu-xhci returns zero for narrow reads — see init.c). The
// helpers below all use 32-bit MMIO; the cap_read{8,16} variants
// do an aligned u32 load and shift the requested lane.

static inline u32 xhci_cap_read32_aligned(const struct XhciController *c, u32 off)
{
    return *(volatile u32 *)(c->cap_regs + (off & ~3u));
}

static inline u8 xhci_cap_read8(const struct XhciController *c, u32 off)
{
    u32 word = xhci_cap_read32_aligned(c, off);
    return (u8)(word >> ((off & 3u) * 8u));
}

static inline u16 xhci_cap_read16(const struct XhciController *c, u32 off)
{
    u32 word = xhci_cap_read32_aligned(c, off);
    return (u16)(word >> ((off & 2u) * 8u));
}

static inline u32 xhci_cap_read32(const struct XhciController *c, u32 off)
{
    return *(volatile u32 *)(c->cap_regs + off);
}

static inline u32 xhci_op_read32(const struct XhciController *c, u32 off)
{
    return *(volatile u32 *)(c->op_regs + off);
}

static inline void xhci_op_write32(const struct XhciController *c, u32 off, u32 v)
{
    *(volatile u32 *)(c->op_regs + off) = v;
}

static inline void xhci_op_write64_pair(const struct XhciController *c, u32 off_lo, u32 off_hi,
                                        u64 v)
{
    xhci_op_write32(c, off_lo, (u32)(v & 0xFFFFFFFFu));
    xhci_op_write32(c, off_hi, (u32)(v >> 32));
}

static inline u32 xhci_rt_read32(const struct XhciController *c, u32 off)
{
    return *(volatile u32 *)(c->runtime_regs + off);
}

static inline void xhci_rt_write32(const struct XhciController *c, u32 off, u32 v)
{
    *(volatile u32 *)(c->runtime_regs + off) = v;
}

static inline void xhci_rt_write64_pair(const struct XhciController *c, u32 off_lo, u32 off_hi,
                                        u64 v)
{
    xhci_rt_write32(c, off_lo, (u32)(v & 0xFFFFFFFFu));
    xhci_rt_write32(c, off_hi, (u32)(v >> 32));
}

// Spin up to ~1M iterations waiting for `(*reg & mask) == expect`.
// Returns false on timeout. Each iteration reads MMIO; with the
// inner nop pad this is a few ms of real time on the daily-driver
// targets — long enough for HCRST + the Run/Stop transition.
[[nodiscard]] static inline bool xhci_spin_for_mask(volatile u32 *reg, u32 mask, u32 expect)
{
    for (u32 i = 0; i < 1000000u; i++) {
        if ((*reg & mask) == expect) {
            return true;
        }
        for (u32 j = 0; j < 100u; j++) {
            __asm__ volatile("nop");
        }
    }
    return false;
}

// Doorbell write (xHCI 1.2 §5.6). doorbells[0] is the Command
// Doorbell (write 0); doorbells[slot_id] is the Slot Doorbell
// (target = endpoint DCI: 1 for EP0, 2*ep+dir for the rest).
static inline void xhci_doorbell_ring(const struct XhciController *c, u32 slot_id, u32 target)
{
    c->doorbells[slot_id] = target;
}

// Write a TRB into a transfer/command ring at index `idx`. The TRB
// is 4 × u32; bit 0 of the *control* word is the Cycle bit, set per
// the caller-supplied PCS via OR'ing XHCI_TRB_CYCLE in.
static inline void xhci_trb_write(volatile u32 *ring, u32 idx, u32 p_lo, u32 p_hi, u32 status,
                                  u32 control)
{
    volatile u32 *trb = &ring[idx * 4];
    trb[0] = p_lo;
    trb[1] = p_hi;
    trb[2] = status;
    // Cycle-bearing word last, so the controller never sees a
    // partially-written TRB (xHCI 1.2 §4.9.3).
    __atomic_thread_fence(__ATOMIC_RELEASE);
    trb[3] = control;
}

// Read a TRB out of the event ring. Returns the four u32 lanes
// through *p_lo/*p_hi/*status/*control.
static inline void xhci_trb_read(const volatile u32 *ring, u32 idx, u32 *p_lo, u32 *p_hi,
                                 u32 *status, u32 *control)
{
    const volatile u32 *trb = &ring[idx * 4];
    *p_lo = trb[0];
    *p_hi = trb[1];
    *status = trb[2];
    *control = trb[3];
}

#endif
