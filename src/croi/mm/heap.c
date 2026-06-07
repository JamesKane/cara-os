// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel heap. Eight size classes (16..2048) backed by slab pages; each
// slab page reserves a 16-byte header so Croi_Free can recover the
// class id from a bare pointer by masking to the page base. Large
// requests (> 2048 bytes) bypass the slab and live in N pages whose
// first 16 bytes hold the LargeHeader so Croi_Free dispatches the
// same way.
//
// Free objects within a slab class form a singly-linked list whose
// next-pointer is stored in the first 8 bytes of each free object, so
// the minimum class size is 16 (covers next + alignment slack).

#include <cara/alloc.h>
#include <cara/list.h>
#include <cara/mm.h>
#include <cara/types.h>

#define SLAB_MAGIC 0x534C4142u  // 'SLAB'
#define LARGE_MAGIC 0x4C415247u // 'LARG'

struct SlabHeader {
    u32 magic; // SLAB_MAGIC
    u8 class_id;
    u8 _pad[3];
    struct MinNode link; // hangs off HeapClass.slab_pages
};

struct LargeHeader {
    u32 magic; // LARGE_MAGIC
    u32 n_pages;
};

static const u32 g_class_sizes[CARA_HEAP_NUM_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048,
};

static struct Heap *g_active_heap = nullptr;

// Optional second heap (the SASOS shared heap) and the VA window it
// owns, so Croi_Free can route a pointer to the right heap by range.
static struct Heap *g_shared_heap = nullptr;
static u64 g_shared_lo = 0;
static u64 g_shared_hi = 0;

void Heap_SetActive(struct Heap *h)
{
    g_active_heap = h;
}

void Heap_RegisterShared(struct Heap *h, u64 lo, u64 hi)
{
    g_shared_heap = h;
    g_shared_lo = lo;
    g_shared_hi = hi;
}

// Translate a freshly-allocated physical page to the VA this heap
// addresses it by, and back. The kernel heap uses the upper-half direct
// map; an arena heap uses its pre-mapped window.
static void *heap_p2v(const struct Heap *h, u64 phys)
{
    if (h->va_base) {
        return (void *)(uptr)(h->va_base + (phys - h->pa_base));
    }
    return Mm_PhysToVirt(phys);
}

static u64 heap_v2p(const struct Heap *h, const void *va)
{
    if (h->va_base) {
        return h->pa_base + ((uptr)va - (uptr)h->va_base);
    }
    return Mm_VirtToPhys(va);
}

[[nodiscard]] int Heap_Init(struct Heap *h, struct PageAllocator *pa)
{
    if (!h || !pa) {
        return CARA_EINVAL;
    }
    *h = (struct Heap){ 0 };
    h->pa = pa;
    for (u32 i = 0; i < CARA_HEAP_NUM_CLASSES; i++) {
        h->classes[i].obj_size = g_class_sizes[i];
        MinList_Init(&h->classes[i].slab_pages);
    }
    return CARA_EOK;
}

[[nodiscard]] int Heap_InitArena(struct Heap *h, struct PageAllocator *pa, u64 va_base, u64 pa_base)
{
    int rc = Heap_Init(h, pa);
    if (rc != CARA_EOK) {
        return rc;
    }
    h->va_base = va_base;
    h->pa_base = pa_base;
    return CARA_EOK;
}

static u32 size_to_class(usize size)
{
    for (u32 i = 0; i < CARA_HEAP_NUM_CLASSES; i++) {
        if (size <= g_class_sizes[i]) {
            return i;
        }
    }
    return CARA_HEAP_NUM_CLASSES; // sentinel: "too large for slabs"
}

// Carve a fresh slab page for class `cid`, push it onto the class's
// slab list, and seed the freelist.
[[nodiscard]] static int slab_grow(struct Heap *h, u32 cid)
{
    u64 phys = Page_Alloc(h->pa, 1);
    if (phys == 0) {
        return CARA_ENOMEM;
    }
    struct SlabHeader *sh = (struct SlabHeader *)heap_p2v(h, phys);
    sh->magic = SLAB_MAGIC;
    sh->class_id = (u8)cid;
    MinList_AddTail(&h->classes[cid].slab_pages, &sh->link);

    // Lay objects starting after the header, aligned to obj_size.
    u32 obj_size = h->classes[cid].obj_size;
    u32 first_off = sizeof(struct SlabHeader);
    if (first_off & (obj_size - 1)) {
        first_off = (first_off + obj_size - 1) & ~(obj_size - 1);
    }
    u32 n_objs = ((u32)CARA_PAGE_SIZE - first_off) / obj_size;
    u8 *page = (u8 *)sh;

    void *prev = nullptr;
    for (u32 i = 0; i < n_objs; i++) {
        void *obj = page + first_off + (usize)i * obj_size;
        *(void **)obj = prev;
        prev = obj;
    }
    h->classes[cid].free_head = prev;
    return CARA_EOK;
}

[[nodiscard]] static void *alloc_from(struct Heap *h, usize size)
{
    if (!h || size == 0) {
        return nullptr;
    }

    u32 cid = size_to_class(size);
    if (cid < CARA_HEAP_NUM_CLASSES) {
        // Slab path.
        if (!h->classes[cid].free_head) {
            if (slab_grow(h, cid) != CARA_EOK) {
                return nullptr;
            }
        }
        void *obj = h->classes[cid].free_head;
        h->classes[cid].free_head = *(void **)obj;
        h->classes[cid].in_flight++;
        if (h->classes[cid].in_flight > h->classes[cid].peak_in_flight) {
            h->classes[cid].peak_in_flight = h->classes[cid].in_flight;
        }
        h->bytes_in_flight += h->classes[cid].obj_size;
        if (h->bytes_in_flight > h->peak_bytes_in_flight) {
            h->peak_bytes_in_flight = h->bytes_in_flight;
        }
        // Zero the object (Page_Alloc zeroed the slab; subsequent reuse
        // requires its own zero pass).
        for (u32 i = 0; i < h->classes[cid].obj_size; i++) {
            ((u8 *)obj)[i] = 0;
        }
        return obj;
    }

    // Large path: bytes + LargeHeader rounded up to whole pages.
    usize total = size + sizeof(struct LargeHeader);
    u32 n_pages = (u32)((total + CARA_PAGE_SIZE - 1) / CARA_PAGE_SIZE);
    u64 phys = Page_Alloc(h->pa, n_pages);
    if (phys == 0) {
        return nullptr;
    }
    struct LargeHeader *lh = (struct LargeHeader *)heap_p2v(h, phys);
    lh->magic = LARGE_MAGIC;
    lh->n_pages = n_pages;
    h->large_in_flight++;
    if (h->large_in_flight > h->large_peak) {
        h->large_peak = h->large_in_flight;
    }
    h->bytes_in_flight += (u64)n_pages * CARA_PAGE_SIZE;
    if (h->bytes_in_flight > h->peak_bytes_in_flight) {
        h->peak_bytes_in_flight = h->bytes_in_flight;
    }
    return (u8 *)lh + sizeof(struct LargeHeader);
}

[[nodiscard]] void *Croi_Alloc(usize size)
{
    return alloc_from(g_active_heap, size);
}

[[nodiscard]] void *Croi_HeapAlloc(struct Heap *h, usize size)
{
    return alloc_from(h, size);
}

void Croi_Free(void *ptr)
{
    if (!ptr) {
        return;
    }
    // Route by VA range: pointers in the registered shared window belong
    // to the shared heap; everything else to the active kernel heap.
    struct Heap *h = g_active_heap;
    if (g_shared_heap && (u64)(uptr)ptr >= g_shared_lo && (u64)(uptr)ptr < g_shared_hi) {
        h = g_shared_heap;
    }
    if (!h) {
        return;
    }
    u8 *page = (u8 *)((uptr)ptr & ~(uptr)(CARA_PAGE_SIZE - 1));
    u32 magic = *(u32 *)page;
    if (magic == SLAB_MAGIC) {
        struct SlabHeader *sh = (struct SlabHeader *)page;
        u32 cid = sh->class_id;
        *(void **)ptr = h->classes[cid].free_head;
        h->classes[cid].free_head = ptr;
        h->classes[cid].in_flight--;
        h->bytes_in_flight -= h->classes[cid].obj_size;
        return;
    }
    if (magic == LARGE_MAGIC) {
        struct LargeHeader *lh = (struct LargeHeader *)page;
        u32 n_pages = lh->n_pages;
        h->large_in_flight--;
        h->bytes_in_flight -= (u64)n_pages * CARA_PAGE_SIZE;
        Page_Free(h->pa, heap_v2p(h, page), n_pages);
        return;
    }
    // Bad magic — caller passed a non-heap pointer or memory was
    // corrupted. Fall through silently for now; once we have a panic
    // path with a logger, this will become Croi_Panic.
}
