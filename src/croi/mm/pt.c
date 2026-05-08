// SPDX-License-Identifier: BSD-2-Clause
//
// Sv39 page-table builder. Per-task tables clone the kernel upper-half
// (L2[256] device, L2[258] RAM RWX — same shape _start.S sets up in
// the boot table) so a satp write keeps S-mode code resolvable, then
// allow Page_Map to populate the lower half page-by-page.

#include <cara/alloc.h>
#include <cara/mm.h>
#include <cara/types.h>

#define ENTRIES_PER_TABLE 512u

// Allocate a zeroed 4 KiB page table page from the page allocator.
// Returns the upper-half VA pointer.
static u64 *alloc_pt_page(void)
{
    extern struct PageAllocator g_page_alloc;
    u64 phys = Page_Alloc(&g_page_alloc, 1);
    if (phys == 0) {
        return nullptr;
    }
    return (u64 *)Mm_PhysToVirt(phys);   // Page_Alloc already zeroed it
}

static void free_pt_page(u64 *page)
{
    extern struct PageAllocator g_page_alloc;
    if (!page) {
        return;
    }
    Page_Free(&g_page_alloc, Mm_VirtToPhys(page), 1);
}

// Build a leaf PTE: PPN goes in bits [53:10], flags in [9:0].
static inline u64 make_leaf(u64 pa, u64 flags)
{
    return ((pa >> 12) << 10) | flags;
}

static inline u64 make_intermediate(u64 child_pa)
{
    return ((child_pa >> 12) << 10) | PTE_V;     // V only; no R/W/X => non-leaf
}

static inline u64 *pte_child(u64 pte)
{
    u64 child_pa = ((pte >> 10) & 0x0FFFFFFFFFFFull) << 12;
    return (u64 *)Mm_PhysToVirt(child_pa);
}

static inline bool pte_present(u64 pte)
{
    return (pte & PTE_V) != 0;
}

static inline bool pte_is_leaf(u64 pte)
{
    return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

// ASID counter. Starts at 1; ASID 0 is reserved for the boot PT in
// case anything ever reuses it. Wraparound is theoretical — we don't
// generate 64K tasks in Tier 2.
static u16 g_next_asid = 1;

[[nodiscard]] struct PageTable *Croi_NewKernelPT(void)
{
    struct PageTable *pt = (struct PageTable *)Croi_Alloc(sizeof(struct PageTable));
    if (!pt) {
        return nullptr;
    }
    pt->root = alloc_pt_page();
    if (!pt->root) {
        Croi_Free(pt);
        return nullptr;
    }
    pt->asid = g_next_asid++;

    // Mirror the boot-PT kernel mapping shape: 1 GiB device block at
    // L2[256] (PA 0) + 1 GiB RWX RAM block at L2[258] (PA 0x80000000).
    pt->root[256] = make_leaf(0x00000000ull, PTE_KERNEL_RW);
    pt->root[258] = make_leaf(0x80000000ull, PTE_KERNEL_RWX);

    return pt;
}

// Recursive free: walk the table, free any non-shared intermediate
// pages, then free the root and the descriptor.
//
// "Non-shared" means anything other than the kernel-upper-half indexes
// 256/258, which are 1 GiB leaves shared with every other PT.
static void free_subtable(u64 *table, int level)
{
    if (level == 0) {
        return;     // L0 leaves are 4 KiB pages; not separately tracked
    }
    for (u32 i = 0; i < ENTRIES_PER_TABLE; i++) {
        u64 pte = table[i];
        if (!pte_present(pte) || pte_is_leaf(pte)) {
            continue;
        }
        u64 *child = pte_child(pte);
        free_subtable(child, level - 1);
        free_pt_page(child);
    }
}

void Croi_DestroyPT(struct PageTable *pt)
{
    if (!pt) {
        return;
    }
    if (pt->root) {
        // Don't recurse into kernel-upper-half (256, 258) — those are
        // 1 GiB leaves shared by every PT.
        for (u32 i = 0; i < 256; i++) {
            u64 pte = pt->root[i];
            if (pte_present(pte) && !pte_is_leaf(pte)) {
                u64 *child = pte_child(pte);
                free_subtable(child, /*level=*/1);
                free_pt_page(child);
            }
        }
        free_pt_page(pt->root);
    }
    Croi_Free(pt);
}

[[nodiscard]] int Page_Map(struct PageTable *pt, u64 va, u64 pa, u64 prot)
{
    if (!pt || !pt->root) {
        return CARA_EINVAL;
    }
    if ((va & (CARA_PAGE_SIZE - 1)) != 0 || (pa & (CARA_PAGE_SIZE - 1)) != 0) {
        return CARA_EINVAL;
    }
    if ((prot & PTE_V) == 0) {
        return CARA_EINVAL;
    }

    u32 i2 = (u32)((va >> 30) & 0x1FFu);
    u32 i1 = (u32)((va >> 21) & 0x1FFu);
    u32 i0 = (u32)((va >> 12) & 0x1FFu);

    // Walk L2 → L1 → L0, allocating intermediate tables on the way.
    u64 *l2 = pt->root;
    u64  l2_pte = l2[i2];

    u64 *l1 = nullptr;
    if (!pte_present(l2_pte)) {
        l1 = alloc_pt_page();
        if (!l1) {
            return CARA_ENOMEM;
        }
        l2[i2] = make_intermediate(Mm_VirtToPhys(l1));
    } else if (pte_is_leaf(l2_pte)) {
        // 1 GiB leaf already covers this VA — refuse to overwrite.
        return CARA_EINVAL;
    } else {
        l1 = pte_child(l2_pte);
    }

    u64  l1_pte = l1[i1];
    u64 *l0 = nullptr;
    if (!pte_present(l1_pte)) {
        l0 = alloc_pt_page();
        if (!l0) {
            return CARA_ENOMEM;
        }
        l1[i1] = make_intermediate(Mm_VirtToPhys(l0));
    } else if (pte_is_leaf(l1_pte)) {
        return CARA_EINVAL;     // 2 MiB leaf — refuse overwrite
    } else {
        l0 = pte_child(l1_pte);
    }

    if (pte_present(l0[i0])) {
        return CARA_EINVAL;     // already mapped
    }
    l0[i0] = make_leaf(pa, prot);
    return CARA_EOK;
}
