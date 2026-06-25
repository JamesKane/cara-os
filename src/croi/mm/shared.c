// SPDX-License-Identifier: BSD-2-Clause
//
// SASOS shared system heap — see cara/shared.h. Kernel-only (uses satp /
// sfence); gated to the rv64 target in src/croi/mm/CMakeLists.txt.

#include <cara/alloc.h>
#include <cara/arch.h>
#include <cara/log.h>
#include <cara/mm.h>
#include <cara/shared.h>
#include <cara/types.h>

static struct PageAllocator g_shared_pa; // bitmap allocator over the arena
static struct Heap g_shared_heap;        // slab heap over the arena window
static u64 g_shared_l1_phys;             // the shared L1 subtree page (phys)
static bool g_shared_up = false;

// Non-leaf PTE pointing at a child table (the arch PTE encoding).
static inline u64 make_intermediate(u64 child_pa)
{
    return arch_pte_table(child_pa);
}

[[nodiscard]] int Croi_Shared_InstallMapping(struct PageTable *pt)
{
    if (!pt || !pt->root || !g_shared_up) {
        return CARA_EINVAL;
    }
    // Point this task's root at the one shared L1 subtree. Every mapping
    // already in the window (and any future one) is instantly visible.
    pt->root[CARA_SHARED_L2_INDEX] = make_intermediate(g_shared_l1_phys);
    return CARA_EOK;
}

[[nodiscard]] void *Croi_AllocShared(usize size)
{
    if (!g_shared_up) {
        return nullptr;
    }
    return Croi_HeapAlloc(&g_shared_heap, size);
}

[[nodiscard]] int Croi_Shared_Init(struct PageAllocator *pa)
{
    if (!pa || g_shared_up) {
        return CARA_EINVAL;
    }

    const u32 arena_pages = (u32)(CARA_SHARED_ARENA_BYTES / CARA_PAGE_SIZE);

    // 1. Reserve the arena (contiguous) + the shared L1 subtree page from
    //    the main pool. These are carved out of main RAM for good.
    u64 arena = Page_Alloc(pa, arena_pages);
    if (arena == 0) {
        LOG_FATAL("shmem", "arena reserve (%u pages) failed", arena_pages);
        return CARA_ENOMEM;
    }
    u64 l1_phys = Page_Alloc(pa, 1);
    if (l1_phys == 0) {
        Page_Free(pa, arena, arena_pages);
        return CARA_ENOMEM;
    }
    g_shared_l1_phys = l1_phys;

    // 2. Install the shared L2 entry into the boot PT, then map the whole
    //    arena RW+U into the window. Installing root[L2] *before* the
    //    Page_Map loop makes Page_Map walk into our shared L1 (rather than
    //    allocate its own), so the L0 tables it adds hang off the shared
    //    subtree and are visible in every PT that later installs root[L2].
    u64 *root = arch_mmu_boot_root();
    if (root[CARA_SHARED_L2_INDEX] & PTE_V) {
        LOG_FATAL("shmem", "boot L2[%u] already populated", CARA_SHARED_L2_INDEX);
        return CARA_EINVAL;
    }
    root[CARA_SHARED_L2_INDEX] = make_intermediate(l1_phys);

    struct PageTable boot_pt = { .root = root, .asid = 0 };
    for (u32 i = 0; i < arena_pages; i++) {
        u64 va = CARA_SHARED_VA_BASE + (u64)i * CARA_PAGE_SIZE;
        u64 phys = arena + (u64)i * CARA_PAGE_SIZE;
        int rc = Page_Map(&boot_pt, va, phys, PTE_USER_RW);
        if (rc != CARA_EOK) {
            LOG_FATAL("shmem", "arena map page %u failed: %d", i, rc);
            return rc;
        }
    }
    arch_mmu_fence();

    // 3. A bitmap allocator + slab heap over the arena. The window is
    //    pre-mapped 1:1 with the arena, so the heap's phys→VA is the fixed
    //    window offset (Heap_InitArena), and pointers it returns are the
    //    lower-half RW+U VAs user mode dereferences.
    struct PhysMap pm = { 0 };
    pm.usable[0].base = arena;
    pm.usable[0].size = CARA_SHARED_ARENA_BYTES;
    pm.n_usable = 1;
    pm.usable_bytes = CARA_SHARED_ARENA_BYTES;
    if (Page_Init(&g_shared_pa, &pm) != CARA_EOK) {
        LOG_FATAL("shmem", "arena Page_Init failed");
        return CARA_EINVAL;
    }
    if (Heap_InitArena(&g_shared_heap, &g_shared_pa, CARA_SHARED_VA_BASE, arena) != CARA_EOK) {
        return CARA_EINVAL;
    }
    Heap_RegisterShared(&g_shared_heap, CARA_SHARED_VA_BASE,
                        CARA_SHARED_VA_BASE + CARA_SHARED_ARENA_BYTES);

    g_shared_up = true;
    LOG_INFO("shmem", "SASOS shared heap: %llu MiB arena at VA 0x%llx (phys 0x%llx)",
             CARA_SHARED_ARENA_BYTES / (1024 * 1024), CARA_SHARED_VA_BASE, arena);
    return CARA_EOK;
}
