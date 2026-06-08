// SPDX-License-Identifier: BSD-2-Clause
//
// QEMU `ramfb` framebuffer bring-up over the fw-cfg MMIO device. QEMU's
// `-device ramfb` exposes an `etc/ramfb` fw-cfg item the guest programs
// (via the fw-cfg DMA interface) with the physical address + geometry of
// a framebuffer it allocated; QEMU then scans that guest memory out to
// the display. This is the Phase 1 path to a real, visible framebuffer
// under `qemu-system-riscv64 -M virt` (which has no simple-framebuffer
// FDT node). Kernel-only.

#ifndef CROI_RAMFB_H
#define CROI_RAMFB_H

#include <cara/types.h>

struct Fdt;
struct DathFramebuffer;
struct PageAllocator;

// Allocate a `width`x`height` XRGB8888 framebuffer from `pa`, program
// QEMU's etc/ramfb to scan it out, and fill `out` with the kernel-VA
// base + geometry (DATH_FMT_RGBA8888). Returns:
//   CARA_EOK        framebuffer is live
//   CARA_ENOTFOUND  no fw-cfg node, or QEMU started without -device ramfb
//   CARA_ENOMEM     couldn't allocate the contiguous framebuffer
//   CARA_EIO        fw-cfg DMA failed
[[nodiscard]] int Croi_Ramfb_Setup(struct DathFramebuffer *out, const struct Fdt *fdt,
                                   struct PageAllocator *pa, u32 width, u32 height);

#endif // CROI_RAMFB_H
