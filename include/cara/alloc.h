// SPDX-License-Identifier: BSD-2-Clause
//
// Cara kernel heap. Size-class slabs for small (≤ 2048 byte) objects;
// large requests go straight to the page allocator with a minimal
// in-place header. Croi_Free does not need a size argument — it reads
// the magic at the start of the containing page to dispatch.

#ifndef CARA_ALLOC_H
#define CARA_ALLOC_H

#include <cara/list.h>
#include <cara/mm.h>
#include <cara/types.h>

constexpr u32 CARA_HEAP_NUM_CLASSES = 8; // 16, 32, 64, 128, 256, 512, 1024, 2048

struct HeapClass {
    u32 obj_size;  // bytes per object in this class
    u32 in_flight; // currently allocated objects
    u32 peak_in_flight;
    void *free_head; // singly-linked free objects (next ptr at object[0..7])
    struct MinList slab_pages;
};

struct Heap {
    struct PageAllocator *pa;
    struct HeapClass classes[CARA_HEAP_NUM_CLASSES];
    u32 large_in_flight;
    u32 large_peak;
    u64 bytes_in_flight;
    u64 peak_bytes_in_flight;

    // Physical→virtual translation for this heap's pages. The default
    // kernel heap leaves these zero and uses the upper-half direct map
    // (Mm_PhysToVirt). The SASOS shared heap (cara/shared.h) sets them
    // so its slab pages resolve to the lower-half RW+U arena VAs user
    // mode can also dereference: va = va_base + (phys - pa_base).
    u64 va_base;
    u64 pa_base;
};

[[nodiscard]] int Heap_Init(struct Heap *h, struct PageAllocator *pa);

// Like Heap_Init, but the heap's pages resolve to a pre-mapped arena
// window [va_base, ...) backing physical [pa_base, ...) instead of the
// kernel direct map. Used by the SASOS shared heap.
[[nodiscard]] int Heap_InitArena(struct Heap *h, struct PageAllocator *pa, u64 va_base,
                                 u64 pa_base);

[[nodiscard]] void *Croi_Alloc(usize size);
void Croi_Free(void *ptr);

// Allocate from a specific heap (the shared heap uses this). Croi_Free
// auto-routes a pointer back to the heap that owns its VA range — see
// Heap_RegisterShared.
[[nodiscard]] void *Croi_HeapAlloc(struct Heap *h, usize size);

// Install the active heap; subsequent Croi_Alloc/Free calls resolve
// against it. Tier 1 only has one Heap; SMP / per-task heaps come later.
void Heap_SetActive(struct Heap *h);

// Register a second heap whose VAs fall in [lo, hi) so Croi_Free routes
// pointers in that range to `h` instead of the active kernel heap. Used
// by the SASOS shared heap so a single Croi_Free works for both.
void Heap_RegisterShared(struct Heap *h, u64 lo, u64 hi);

#endif
