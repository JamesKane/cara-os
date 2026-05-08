// SPDX-License-Identifier: BSD-2-Clause
//
// Cara physical memory map. Built once during early boot from the FDT;
// describes the contiguous physical RAM runs the page allocator may
// hand out, after carving out: kernel image, DTB blob, /memreserve/
// entries, and /reserved-memory/* nodes.

#ifndef CARA_MM_H
#define CARA_MM_H

#include <cara/fdt.h>
#include <cara/types.h>

#define CARA_MAX_PHYS_BANKS    8
#define CARA_MAX_PHYS_RUNS    32   // banks + reserved carve-outs can fragment

struct PhysRun {
    u64 base;       // physical start, page-aligned
    u64 size;       // bytes, page-multiple
};

struct PhysMap {
    struct PhysRun bank[CARA_MAX_PHYS_BANKS];   // raw /memory@* nodes
    u32            n_banks;
    struct PhysRun usable[CARA_MAX_PHYS_RUNS];  // banks - reserved - kernel
    u32            n_usable;
    u64            total_bytes;                 // sum of bank.size
    u64            usable_bytes;                // sum of usable.size
};

// Build the physical memory map. `kernel_phys_{start,end}` and
// `dtb_phys_{start,end}` describe the regions to carve out so the
// allocator never hands them back. Walks /memory@*, /memreserve/, and
// /reserved-memory/*. Returns CARA_EOK on success or a negative error
// when the FDT is unsuitable (no memory nodes, too many runs, etc.).
[[nodiscard]] int
Mm_PhysMapFromFdt(struct PhysMap *out, const struct Fdt *fdt,
                  u64 kernel_phys_start, u64 kernel_phys_end,
                  u64 dtb_phys_start, u64 dtb_phys_end);

#endif
