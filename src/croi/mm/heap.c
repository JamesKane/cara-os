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

#define SLAB_MAGIC   0x534C4142u    // 'SLAB'
#define LARGE_MAGIC  0x4C415247u    // 'LARG'

struct SlabHeader {
    u32 magic;                   // SLAB_MAGIC
    u8  class_id;
    u8  _pad[3];
    struct ListNode link;        // hangs off HeapClass.slab_pages
};

struct LargeHeader {
    u32 magic;                   // LARGE_MAGIC
    u32 n_pages;
};

static const u32 g_class_sizes[CARA_HEAP_NUM_CLASSES] = {
    16, 32, 64, 128, 256, 512, 1024, 2048,
};

static struct Heap *g_active_heap = nullptr;

void Heap_SetActive(struct Heap *h)
{
    g_active_heap = h;
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
        ListInit(&h->classes[i].slab_pages);
    }
    return CARA_EOK;
}

static u32 size_to_class(usize size)
{
    for (u32 i = 0; i < CARA_HEAP_NUM_CLASSES; i++) {
        if (size <= g_class_sizes[i]) {
            return i;
        }
    }
    return CARA_HEAP_NUM_CLASSES;       // sentinel: "too large for slabs"
}

// Carve a fresh slab page for class `cid`, push it onto the class's
// slab list, and seed the freelist.
[[nodiscard]] static int slab_grow(struct Heap *h, u32 cid)
{
    u64 phys = Page_Alloc(h->pa, 1);
    if (phys == 0) {
        return CARA_ENOMEM;
    }
    struct SlabHeader *sh = (struct SlabHeader *)Mm_PhysToVirt(phys);
    sh->magic = SLAB_MAGIC;
    sh->class_id = (u8)cid;
    ListAddTail(&h->classes[cid].slab_pages, &sh->link);

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

[[nodiscard]] void *Croi_Alloc(usize size)
{
    struct Heap *h = g_active_heap;
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
    struct LargeHeader *lh = (struct LargeHeader *)Mm_PhysToVirt(phys);
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

void Croi_Free(void *ptr)
{
    if (!ptr) {
        return;
    }
    struct Heap *h = g_active_heap;
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
        Page_Free(h->pa, Mm_VirtToPhys(page), n_pages);
        return;
    }
    // Bad magic — caller passed a non-heap pointer or memory was
    // corrupted. Fall through silently for now; once we have a panic
    // path with a logger, this will become Croi_Panic.
}
