// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(shared_heap): the SASOS shared heap (cara/shared.h). Croi_
// Shared_Init ran at boot, so allocations land in the lower-half RW+U
// window. Verifies the returned VAs are in the window, that the kernel
// can write and read them back (proving the mapping + sstatus.SUM access),
// that free routes correctly, and that the large (multi-page) path works.
// U-mode visibility of these pointers is proved separately by userexec
// once AllocMem is repointed (S2).

#include <cara/alloc.h>
#include <cara/shared.h>
#include <cara/test.h>
#include <cara/types.h>

static bool in_window(const void *p)
{
    u64 va = (u64)(uptr)p;
    return va >= CARA_SHARED_VA_BASE && va < CARA_SHARED_VA_BASE + CARA_SHARED_ARENA_BYTES;
}

KERNEL_TEST(shared_heap)
{
    // Slab-path allocation lands in the shared window and round-trips.
    u8 *p = (u8 *)Croi_AllocShared(128);
    TEST_ASSERT(ctx, p != nullptr, "AllocShared(128)");
    TEST_ASSERT(ctx, in_window(p), "small alloc VA in shared window");
    for (u32 i = 0; i < 128; i++) {
        p[i] = (u8)(i * 7u + 1u);
    }
    bool ok = true;
    for (u32 i = 0; i < 128; i++) {
        if (p[i] != (u8)(i * 7u + 1u)) {
            ok = false;
        }
    }
    TEST_ASSERT(ctx, ok, "small write/read round-trip");

    // Free routes back to the shared heap (by VA range), and the slab is
    // reused for a same-class request.
    Croi_Free(p);
    void *p2 = Croi_AllocShared(128);
    TEST_ASSERT(ctx, p2 != nullptr, "re-AllocShared after free");
    TEST_ASSERT(ctx, in_window(p2), "reused alloc still in window");
    Croi_Free(p2);

    // Large path (> 2048 bytes) — multiple contiguous pages.
    u64 *big = (u64 *)Croi_AllocShared(8192);
    TEST_ASSERT(ctx, big != nullptr, "AllocShared(8192)");
    TEST_ASSERT(ctx, in_window(big), "large alloc VA in shared window");
    big[0] = 0xCA1AB1A5ull;
    big[1023] = 0xDEADBEEFull; // last u64 of the 8 KiB span
    TEST_ASSERT(ctx, big[0] == 0xCA1AB1A5ull && big[1023] == 0xDEADBEEFull,
                "large write/read round-trip");
    Croi_Free(big);

    // Distinct allocations get distinct addresses.
    void *a = Croi_AllocShared(64);
    void *b = Croi_AllocShared(64);
    TEST_ASSERT(ctx, a && b && a != b, "distinct allocs are distinct");
    Croi_Free(a);
    Croi_Free(b);
}
