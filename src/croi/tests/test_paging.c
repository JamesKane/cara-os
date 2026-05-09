// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(paging_smoke): build two distinct page tables, map
// different physical pages at the same lower-half VA in each, flip
// satp manually between them, and verify the data the same VA points
// at differs. Exercises walk-and-allocate (intermediate L1/L0 tables
// are demand-allocated by Page_Map), the Sv39 satp encoding, and the
// fact that the kernel upper-half mapping survives the satp flip.

#include "../kernel.h"

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/sched.h>
#include <cara/test.h>
#include <cara/types.h>

#define TEST_VA 0x40000000ull // 1 GiB into the lower half

static inline void csrw_satp(u64 v)
{
    __asm__ volatile("csrw satp, %0" : : "r"(v) : "memory");
    __asm__ volatile("sfence.vma zero, zero" : : : "memory");
}

static inline u64 csrr_satp(void)
{
    u64 v;
    __asm__ volatile("csrr %0, satp" : "=r"(v));
    return v;
}

KERNEL_TEST(paging_smoke)
{
    // Save the current satp so we can restore the boot PT after the test.
    u64 saved_satp = csrr_satp();

    struct PageTable *pt_a = Croi_NewKernelPT();
    struct PageTable *pt_b = Croi_NewKernelPT();
    TEST_ASSERT(ctx, pt_a != nullptr, "NewKernelPT a failed");
    TEST_ASSERT(ctx, pt_b != nullptr, "NewKernelPT b failed");

    u64 page_a = Page_Alloc(&g_page_alloc, 1);
    u64 page_b = Page_Alloc(&g_page_alloc, 1);
    TEST_ASSERT(ctx, page_a != 0, "Page_Alloc page_a failed");
    TEST_ASSERT(ctx, page_b != 0, "Page_Alloc page_b failed");

    // Stamp distinct data via the kernel direct map.
    *(volatile u32 *)Mm_PhysToVirt(page_a) = 0xAAAAAAAAu;
    *(volatile u32 *)Mm_PhysToVirt(page_b) = 0xBBBBBBBBu;

    TEST_ASSERT(ctx, Page_Map(pt_a, TEST_VA, page_a, PTE_KERNEL_RW) == CARA_EOK,
                "Page_Map a failed");
    TEST_ASSERT(ctx, Page_Map(pt_b, TEST_VA, page_b, PTE_KERNEL_RW) == CARA_EOK,
                "Page_Map b failed");

    // Switch to pt_a and read TEST_VA — should observe page_a's data.
    csrw_satp(Sv39_Satp(pt_a));
    u32 val_a = *(volatile u32 *)TEST_VA;

    // Switch to pt_b and read same VA — should observe page_b's data.
    csrw_satp(Sv39_Satp(pt_b));
    u32 val_b = *(volatile u32 *)TEST_VA;

    // Restore the boot PT before any further kernel code or test.
    csrw_satp(saved_satp);

    TEST_ASSERT(ctx, val_a == 0xAAAAAAAAu, "pt_a: VA did not see page_a");
    TEST_ASSERT(ctx, val_b == 0xBBBBBBBBu, "pt_b: VA did not see page_b");

    // Page_Map twice on the same VA in the same PT must fail.
    int rc = Page_Map(pt_a, TEST_VA, page_a, PTE_KERNEL_RW);
    TEST_ASSERT(ctx, rc != CARA_EOK, "double-map should refuse");

    // Cleanup.
    Croi_DestroyPT(pt_a);
    Croi_DestroyPT(pt_b);
    Page_Free(&g_page_alloc, page_a, 1);
    Page_Free(&g_page_alloc, page_b, 1);
}
