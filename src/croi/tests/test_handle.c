// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(handle_smoke): exercises the per-task handle table.
// Opens N handles, looks each up, closes half, verifies stale handles
// fail with EBADF, re-opens into freed slots and asserts the
// generation bump made the new handle distinct from the old one.

#include <cara/kobj.h>
#include <cara/sched.h>
#include <cara/test.h>
#include <cara/types.h>

#define N 10

static struct Kobj g_kobjs[N];
static Handle      g_hs[N];
static Handle      g_hs2[N];

KERNEL_TEST(handle_smoke)
{
    struct HandleTable *ht = &Sched_Current()->handles;

    // Initialise N stack-resident Kobjs of varying types.
    for (u32 i = 0; i < N; i++) {
        Kobj_Init(&g_kobjs[i],
                  (i & 1) ? KOBJ_SIGNAL : KOBJ_TASK,
                  /*destroy=*/nullptr);
    }

    // Open all N handles.
    for (u32 i = 0; i < N; i++) {
        TEST_ASSERT(ctx, HandleTable_Open(ht, &g_kobjs[i], &g_hs[i]) == CARA_EOK,
                    "open failed");
    }

    // Lookup each, expect type-checked match returns the right Kobj.
    for (u32 i = 0; i < N; i++) {
        struct Kobj *k = nullptr;
        KobjType    expected = (i & 1) ? KOBJ_SIGNAL : KOBJ_TASK;
        TEST_ASSERT(ctx,
                    HandleTable_Lookup(ht, g_hs[i], expected, &k) == CARA_EOK,
                    "lookup with correct type failed");
        TEST_ASSERT(ctx, k == &g_kobjs[i], "lookup returned wrong target");
    }

    // Type mismatch fails with EBADF.
    {
        struct Kobj *k = nullptr;
        TEST_ASSERT(ctx,
                    HandleTable_Lookup(ht, g_hs[0], KOBJ_MSGPORT, &k)
                        == CARA_EBADF,
                    "type mismatch should EBADF");
    }

    // Refcount: each open bumped from 1 to 2.
    for (u32 i = 0; i < N; i++) {
        TEST_ASSERT(ctx, g_kobjs[i].refcount == 2u,
                    "refcount not bumped on Open");
    }

    // Close half of the handles.
    for (u32 i = 0; i < N / 2; i++) {
        TEST_ASSERT(ctx, HandleTable_Close(ht, g_hs[i]) == CARA_EOK,
                    "close failed");
    }

    // Closed half: refcount returned to 1, lookups now return EBADF.
    for (u32 i = 0; i < N / 2; i++) {
        TEST_ASSERT(ctx, g_kobjs[i].refcount == 1u,
                    "refcount not decremented on Close");
        struct Kobj *k = nullptr;
        TEST_ASSERT(ctx,
                    HandleTable_Lookup(ht, g_hs[i], KOBJ_NONE, &k)
                        == CARA_EBADF,
                    "stale handle should EBADF");
    }

    // Re-open into freed slots — verify the generation bumped so the
    // new handle differs from the old one.
    for (u32 i = 0; i < N / 2; i++) {
        TEST_ASSERT(ctx,
                    HandleTable_Open(ht, &g_kobjs[i], &g_hs2[i]) == CARA_EOK,
                    "reopen failed");
        TEST_ASSERT(ctx, g_hs2[i] != g_hs[i],
                    "handle reused without generation bump");
    }

    // Cleanup: close everything we opened.
    for (u32 i = 0; i < N / 2; i++) {
        TEST_ASSERT(ctx, HandleTable_Close(ht, g_hs2[i]) == CARA_EOK,
                    "cleanup close (half-1) failed");
    }
    for (u32 i = N / 2; i < N; i++) {
        TEST_ASSERT(ctx, HandleTable_Close(ht, g_hs[i]) == CARA_EOK,
                    "cleanup close (half-2) failed");
    }
    // After all closes: refcount restored to 1 (caller's only).
    for (u32 i = 0; i < N; i++) {
        TEST_ASSERT(ctx, g_kobjs[i].refcount == 1u,
                    "refcount not restored after all closes");
    }
}
