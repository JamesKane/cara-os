// SPDX-License-Identifier: BSD-2-Clause
//
// Bitmap-backed physical page allocator. Each PhysMap usable run owns
// its own bitmap, stored in the first few pages of the run itself; that
// gives O(n_pages / 64) inspection cost per run and zero static state
// proportional to RAM size.
//
// Allocations are zeroed before return. Multi-page allocations require
// contiguous free pages within a single run; cross-run allocations are
// not supported (the caller can ask for at most run.data_pages pages).

#include <cara/mm.h>
#include <cara/types.h>

#define BITS_PER_BYTE 8u

static u64 align_up_u64(u64 v, u64 a)
{
    return (v + a - 1) & ~(a - 1);
}

static bool bit_get(const u8 *bm, u32 i)
{
    return (bm[i >> 3] >> (i & 7)) & 1u;
}

static void bit_set(u8 *bm, u32 i)
{
    bm[i >> 3] |= (u8)(1u << (i & 7));
}

static void bit_clr(u8 *bm, u32 i)
{
    bm[i >> 3] &= (u8) ~(1u << (i & 7));
}

static void zero_bytes(u8 *p, usize n)
{
    while (n--) {
        *p++ = 0;
    }
}

static void zero_pages_at(u64 phys, u32 n_pages)
{
    u8 *p = (u8 *)Mm_PhysToVirt(phys);
    zero_bytes(p, (usize)n_pages * CARA_PAGE_SIZE);
}

[[nodiscard]] int Page_Init(struct PageAllocator *pa, const struct PhysMap *pm)
{
    if (!pa || !pm) {
        return CARA_EINVAL;
    }
    *pa = (struct PageAllocator){ 0 };

    for (u32 i = 0; i < pm->n_usable; i++) {
        if (pa->n_runs >= CARA_MAX_PHYS_RUNS) {
            return CARA_ERANGE;
        }
        const struct PhysRun *src = &pm->usable[i];
        if ((src->base & (CARA_PAGE_SIZE - 1)) != 0 || (src->size & (CARA_PAGE_SIZE - 1)) != 0) {
            return CARA_EINVAL;
        }
        u32 n_pages = (u32)(src->size / CARA_PAGE_SIZE);
        if (n_pages < 2) {
            // Not even one allocatable page after carving the bitmap
            // out of the run; skip it.
            continue;
        }
        u32 bitmap_bytes = (n_pages + BITS_PER_BYTE - 1) / BITS_PER_BYTE;
        u32 bitmap_pages = (u32)align_up_u64(bitmap_bytes, CARA_PAGE_SIZE) / (u32)CARA_PAGE_SIZE;
        if (bitmap_pages >= n_pages) {
            continue;
        }
        u32 data_pages = n_pages - bitmap_pages;

        struct PageRun *r = &pa->runs[pa->n_runs++];
        r->base = src->base;
        r->n_pages = n_pages;
        r->bitmap_pages = bitmap_pages;
        r->bitmap = (u8 *)Mm_PhysToVirt(src->base);
        r->data_pages = data_pages;
        r->free_pages = data_pages;

        // Zero the bitmap (all pages free).
        zero_bytes(r->bitmap, (usize)bitmap_pages * CARA_PAGE_SIZE);

        pa->total_data_pages += data_pages;
        pa->free_pages += data_pages;
    }

    return pa->n_runs > 0 ? CARA_EOK : CARA_ENOTFOUND;
}

[[nodiscard]] u64 Page_Alloc(struct PageAllocator *pa, u32 n_pages)
{
    if (!pa || n_pages == 0) {
        return 0;
    }
    for (u32 ri = 0; ri < pa->n_runs; ri++) {
        struct PageRun *r = &pa->runs[ri];
        if (r->free_pages < n_pages) {
            continue;
        }
        // Linear first-fit scan for n_pages contiguous zero bits.
        u32 run_start = 0;
        u32 run_len = 0;
        for (u32 i = 0; i < r->data_pages; i++) {
            if (!bit_get(r->bitmap, i)) {
                if (run_len == 0) {
                    run_start = i;
                }
                run_len++;
                if (run_len == n_pages) {
                    for (u32 k = 0; k < n_pages; k++) {
                        bit_set(r->bitmap, run_start + k);
                    }
                    r->free_pages -= n_pages;
                    pa->free_pages -= n_pages;
                    u64 in_flight = pa->total_data_pages - pa->free_pages;
                    if (in_flight > pa->peak_in_flight_pages) {
                        pa->peak_in_flight_pages = in_flight;
                    }
                    u64 phys = r->base + (u64)(r->bitmap_pages + run_start) * CARA_PAGE_SIZE;
                    zero_pages_at(phys, n_pages);
                    return phys;
                }
            } else {
                run_len = 0;
            }
        }
    }
    return 0;
}

void Page_Free(struct PageAllocator *pa, u64 phys, u32 n_pages)
{
    if (!pa || phys == 0 || n_pages == 0) {
        return;
    }
    for (u32 ri = 0; ri < pa->n_runs; ri++) {
        struct PageRun *r = &pa->runs[ri];
        u64 data_lo = r->base + (u64)r->bitmap_pages * CARA_PAGE_SIZE;
        u64 data_hi = r->base + (u64)r->n_pages * CARA_PAGE_SIZE;
        if (phys < data_lo || phys >= data_hi) {
            continue;
        }
        u64 offset_pages = (phys - data_lo) / CARA_PAGE_SIZE;
        for (u32 k = 0; k < n_pages; k++) {
            bit_clr(r->bitmap, (u32)offset_pages + k);
        }
        r->free_pages += n_pages;
        pa->free_pages += n_pages;
        return;
    }
}
