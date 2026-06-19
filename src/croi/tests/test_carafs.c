// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(carafs_*): the CaraFS core driven over the real NVMe
// controller (F5). Boot-time bring-up (Croi_Carafs_BringUp in entry.c)
// mounts — or formats then mounts — the volume on NSID 1; these tests
// assert that mount and then exercise the filesystem end to end on
// device storage.
//
// carafs_persist is the reboot test: it seeds a marker file on the
// first boot and verifies it on the next, so the smoke harness boots
// the *same* NVMe image twice and greps the verify line. Needs
// `-device nvme,...` — without it the bring-up never ran and these
// tests fail (expected, same caveat as the nvme_* / xhci tests).

#include <cara/carafs.h>
#include <cara/carafs_bind.h>
#include <cara/log.h>
#include <cara/test.h>
#include <cara/types.h>

constexpr u32 MARKER_LEN = 64;

static u8 marker_byte(u32 i)
{
    return (u8)(0x5Au ^ (i * 37u) ^ (i >> 3));
}

KERNEL_TEST(carafs_mount)
{
    TEST_ASSERT(ctx, g_carafs_mounted, "CaraFS not mounted at boot (no nvme device?)");
    struct CarafsStat st;
    TEST_ASSERT(ctx, Carafs_CnodeStat(&g_carafs, g_carafs.sb.root_cnode, &st) == CARA_EOK,
                "stat root");
    TEST_ASSERT(ctx, st.type == CARAFS_T_DIR, "root is not a directory");
}

// Self-contained create / write / read-back / delete cycle on device
// storage. Idempotent across reboots: clears any leftover from a prior
// aborted run first.
KERNEL_TEST(carafs_io)
{
    TEST_ASSERT(ctx, g_carafs_mounted, "CaraFS not mounted");
    u64 root = g_carafs.sb.root_cnode;

    u64 cn;
    if (Carafs_DirLookup(&g_carafs, root, "ktest", 5, &cn, nullptr) == CARA_EOK) {
        TEST_ASSERT(ctx, Carafs_DirRemove(&g_carafs, root, "ktest", 5) == CARA_EOK,
                    "clear stale ktest");
    }
    TEST_ASSERT(ctx, Carafs_DirCreate(&g_carafs, root, "ktest", 5, CARAFS_T_FILE, &cn) == CARA_EOK,
                "create ktest");

    // A few KiB to span more than one block (exercises the bounce path).
    static u8 wbuf[6000];
    static u8 rbuf[6000];
    for (u32 i = 0; i < sizeof(wbuf); i++) {
        wbuf[i] = (u8)(i * 31u + 7u);
    }
    TEST_ASSERT(ctx, Carafs_FileWrite(&g_carafs, cn, 0, wbuf, sizeof(wbuf)) == CARA_EOK,
                "write ktest");

    usize got = 0;
    TEST_ASSERT(ctx,
                Carafs_FileRead(&g_carafs, cn, 0, rbuf, sizeof(rbuf), &got) == CARA_EOK &&
                    got == sizeof(rbuf),
                "read ktest");
    bool match = true;
    for (u32 i = 0; i < sizeof(wbuf); i++) {
        if (rbuf[i] != wbuf[i]) {
            match = false;
            break;
        }
    }
    TEST_ASSERT(ctx, match, "ktest readback mismatch");

    TEST_ASSERT(ctx, Carafs_DirRemove(&g_carafs, root, "ktest", 5) == CARA_EOK, "remove ktest");
    TEST_ASSERT(ctx, Carafs_DirLookup(&g_carafs, root, "ktest", 5, &cn, nullptr) == CARA_ENOENT,
                "ktest gone");
}

// Reboot persistence: seed on the first boot, verify on the next. Both
// outcomes pass the test; the smoke harness distinguishes them by the
// log line and asserts the verify line appears on the second boot.
KERNEL_TEST(carafs_persist)
{
    TEST_ASSERT(ctx, g_carafs_mounted, "CaraFS not mounted");
    u64 root = g_carafs.sb.root_cnode;

    u64 cn;
    int r = Carafs_DirLookup(&g_carafs, root, "persist", 7, &cn, nullptr);
    if (r == CARA_ENOENT) {
        TEST_ASSERT(ctx,
                    Carafs_DirCreate(&g_carafs, root, "persist", 7, CARAFS_T_FILE, &cn) == CARA_EOK,
                    "create persist marker");
        u8 payload[MARKER_LEN];
        for (u32 i = 0; i < MARKER_LEN; i++) {
            payload[i] = marker_byte(i);
        }
        TEST_ASSERT(ctx, Carafs_FileWrite(&g_carafs, cn, 0, payload, MARKER_LEN) == CARA_EOK,
                    "write persist marker");
        // Checkpoint so the marker is durable on the medium for the
        // next boot (flushes home + advances the journal).
        TEST_ASSERT(ctx, Carafs_Sync(&g_carafs) == CARA_EOK, "sync persist marker");
        LOG_INFO("carafs", "persist marker seeded (first boot)");
        return;
    }
    TEST_ASSERT(ctx, r == CARA_EOK, "lookup persist marker");
    u8 back[MARKER_LEN];
    usize got = 0;
    TEST_ASSERT(ctx,
                Carafs_FileRead(&g_carafs, cn, 0, back, MARKER_LEN, &got) == CARA_EOK &&
                    got == MARKER_LEN,
                "read persist marker");
    bool match = true;
    for (u32 i = 0; i < MARKER_LEN; i++) {
        if (back[i] != marker_byte(i)) {
            match = false;
            break;
        }
    }
    TEST_ASSERT(ctx, match, "persist marker content mismatch across reboot");
    LOG_INFO("carafs", "persist marker verified across reboot");
}
