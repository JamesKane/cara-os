// SPDX-License-Identifier: BSD-2-Clause
//
// In-kernel tests for the page allocator and the size-class heap.
// Use only public APIs (Croi_Alloc/Free, Page_Alloc/Free) plus the
// kernel-private g_page_alloc / g_heap globals declared in kernel.h.

#include "../kernel.h"

#include <cara/alloc.h>
#include <cara/mm.h>
#include <cara/test.h>
#include <cara/types.h>

KERNEL_TEST(pagealloc_smoke)
{
    u64 saved = g_page_alloc.free_pages;
    u64 pages[16];

    for (u32 i = 0; i < 16; i++) {
        pages[i] = Page_Alloc(&g_page_alloc, 1);
        TEST_ASSERT(ctx, pages[i] != 0, "Page_Alloc returned 0");
        for (u32 j = 0; j < i; j++) {
            TEST_ASSERT(ctx, pages[j] != pages[i], "duplicate page returned");
        }
        TEST_ASSERT(ctx, (pages[i] & (CARA_PAGE_SIZE - 1)) == 0, "page not aligned");
    }
    TEST_ASSERT(ctx, g_page_alloc.free_pages == saved - 16,
                "free count wrong after 16 single-page allocs");

    for (u32 i = 0; i < 16; i++) {
        Page_Free(&g_page_alloc, pages[i], 1);
    }
    TEST_ASSERT(ctx, g_page_alloc.free_pages == saved, "free count not restored after free");

    u64 multi = Page_Alloc(&g_page_alloc, 4);
    TEST_ASSERT(ctx, multi != 0, "4-page contiguous alloc failed");
    TEST_ASSERT(ctx, (multi & (CARA_PAGE_SIZE - 1)) == 0, "multi-page not aligned");
    Page_Free(&g_page_alloc, multi, 4);
    TEST_ASSERT(ctx, g_page_alloc.free_pages == saved, "free count wrong after multi-free");
}

KERNEL_TEST(heap_smoke)
{
    u64 saved = g_heap.bytes_in_flight;
    void *ptrs[10];
    const usize sizes[] = { 8, 16, 17, 32, 100, 256, 1000, 2048, 4096, 16384 };

    for (u32 i = 0; i < 10; i++) {
        ptrs[i] = Croi_Alloc(sizes[i]);
        TEST_ASSERT(ctx, ptrs[i] != nullptr, "Croi_Alloc returned null");
        // Touch the memory.
        for (u32 k = 0; k < (u32)sizes[i]; k++) {
            ((u8 *)ptrs[i])[k] = (u8)(k & 0xFF);
        }
    }
    for (u32 i = 0; i < 10; i++) {
        Croi_Free(ptrs[i]);
    }
    TEST_ASSERT(ctx, g_heap.bytes_in_flight == saved,
                "bytes_in_flight not restored after mixed-size cycle");

    // Slab grow: 200 64-byte allocations require multiple slab pages.
    void *many[200];
    for (u32 i = 0; i < 200; i++) {
        many[i] = Croi_Alloc(64);
        TEST_ASSERT(ctx, many[i] != nullptr, "Croi_Alloc(64) failed mid-stress");
    }
    for (u32 i = 0; i < 200; i++) {
        Croi_Free(many[i]);
    }
    TEST_ASSERT(ctx, g_heap.bytes_in_flight == saved,
                "bytes_in_flight wrong after slab-grow stress");
}
