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

#define CARA_PAGE_SIZE         4096ull
#define CARA_KERNEL_VA_OFFSET  0xFFFFFFC000000000ull

// All physical RAM is mapped into the kernel direct-map region by the
// boot Sv39 PT (L2[256..259]); for any physical address P below 4 GiB,
// P + KERNEL_VA_OFFSET is its kernel-virtual alias.
static inline void *Mm_PhysToVirt(u64 phys)
{
    return (void *)(uptr)(phys + CARA_KERNEL_VA_OFFSET);
}

static inline u64 Mm_VirtToPhys(const void *virt)
{
    return (u64)(uptr)virt - CARA_KERNEL_VA_OFFSET;
}

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

// ---- Page allocator -------------------------------------------------------
//
// Bitmap-backed allocator. Each PhysMap usable run gets its own bitmap
// stored at the start of the run itself, paying a few KiB per GiB to keep
// allocator state out of the kernel image. Allocations are zeroed before
// return.

struct PageRun {
    u64  base;            // page-aligned physical base of the run
    u32  n_pages;         // total pages in the run (incl. bitmap)
    u32  bitmap_pages;    // pages reserved at run start to hold the bitmap
    u8  *bitmap;          // upper-half VA pointer (n_data bits, packed)
    u32  data_pages;      // n_pages - bitmap_pages (allocatable count)
    u32  free_pages;      // currently free in this run
};

struct PageAllocator {
    struct PageRun runs[CARA_MAX_PHYS_RUNS];
    u32  n_runs;
    u64  total_data_pages;
    u64  free_pages;
    u64  peak_in_flight_pages;
};

[[nodiscard]] int Page_Init(struct PageAllocator *pa, const struct PhysMap *pm);

// Allocate `n_pages` contiguous physical pages. Returns the physical
// base address on success, or 0 on failure. The mapping at the
// kernel direct-map (Mm_PhysToVirt) is already valid.
[[nodiscard]] u64 Page_Alloc(struct PageAllocator *pa, u32 n_pages);

// Free `n_pages` previously returned by Page_Alloc. `phys` must equal
// the address Page_Alloc returned and `n_pages` must match.
void Page_Free(struct PageAllocator *pa, u64 phys, u32 n_pages);

// ---- Sv39 page tables ------------------------------------------------------

#define PTE_V    0x001ull
#define PTE_R    0x002ull
#define PTE_W    0x004ull
#define PTE_X    0x008ull
#define PTE_U    0x010ull       // user-accessible
#define PTE_G    0x020ull       // global mapping (kernel half)
#define PTE_A    0x040ull
#define PTE_D    0x080ull

#define PTE_KERNEL_RWX (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PTE_KERNEL_RW  (PTE_V | PTE_R | PTE_W |         PTE_G | PTE_A | PTE_D)
#define PTE_USER_RW    (PTE_V | PTE_R | PTE_W | PTE_U |         PTE_A | PTE_D)
#define PTE_USER_RX    (PTE_V | PTE_R |         PTE_X | PTE_U |         PTE_A)
#define PTE_USER_RO    (PTE_V | PTE_R |                 PTE_U |         PTE_A)

struct PageTable {
    u64 *root;             // upper-half VA pointer to the 4 KiB L2 root
    u16  asid;
};

// Allocate a fresh kernel-flavoured page table: zeroed root with the
// kernel-upper-half entries (L2[256] device + L2[258] RAM RWX) already
// installed so a satp write to it keeps S-mode code resolvable. Lower
// half entries are zero — caller maps user regions via Page_Map.
[[nodiscard]] struct PageTable *Croi_NewKernelPT(void);

// Free a page table's resources (intermediate L1/L0 pages walked into
// during Page_Map calls, plus the root). Does not unmap the kernel
// upper half — those leaves are shared globally.
void Croi_DestroyPT(struct PageTable *pt);

// Map a single 4 KiB page. va and pa must be page-aligned. prot is a
// PTE flag mask (typically PTE_USER_RW or PTE_KERNEL_RW). Walks and
// allocates intermediate L1/L0 tables as needed.
[[nodiscard]] int Page_Map(struct PageTable *pt, u64 va, u64 pa, u64 prot);

// Compose an Sv39 satp value: (mode=8 << 60) | (asid << 44) | ppn.
static inline u64 Sv39_Satp(const struct PageTable *pt)
{
    u64 ppn = Mm_VirtToPhys(pt->root) >> 12;
    return (8ull << 60) | ((u64)pt->asid << 44) | ppn;
}

#endif
