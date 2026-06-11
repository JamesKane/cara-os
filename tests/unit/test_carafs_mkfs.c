// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS F1: mkfs → fsck round trip on a memory bdev, plus targeted
// corruption — every metadata block class mkfs lays down must be
// individually detected by fsck when damaged.

#include <cara/carafs.h>
#include <cara/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

constexpr u32 BS = 4096;
constexpr u64 N_BLOCKS = (16ull << 20) / BS; // 16 MiB volume

static u8 *volume;
static u8 scratch[2 * BS];

static void make_bdev(struct CarafsBdev *bdev, struct CarafsMemBdev *state)
{
    Carafs_MemBdev_Init(bdev, state, volume, BS, N_BLOCKS);
}

static int run_mkfs(struct CarafsBdev *bdev)
{
    struct CarafsMkfsOpts opts = {
        .name = "Work",
        .name_len = 4,
        .now_ns = 123456789000ull,
    };
    return Carafs_Mkfs(bdev, &opts, scratch, BS);
}

static u32 fsck_errors(struct CarafsBdev *bdev)
{
    struct CarafsFsckReport rep;
    int rc = Carafs_Fsck(bdev, scratch, sizeof(scratch), &rep);
    CHECK(rc == CARA_EOK, "fsck must be able to run");
    return rep.errors;
}

static void test_roundtrip(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    make_bdev(&bdev, &state);

    CHECK(run_mkfs(&bdev) == CARA_EOK, "mkfs succeeds");
    CHECK(fsck_errors(&bdev) == 0, "fresh volume fscks clean");

    // Superblock spot checks.
    const struct CarafsSuperblock *sb = (const struct CarafsSuperblock *)volume;
    CHECK(memcmp(sb->magic, CARAFS_SB_MAGIC, 8) == 0, "sb magic");
    CHECK(sb->total_blocks == N_BLOCKS, "sb total_blocks");
    CHECK(sb->name_len == 4 && memcmp(sb->name, "Work", 4) == 0, "volume name");
    CHECK(sb->state == CARAFS_STATE_CLEAN, "fresh volume is CLEAN");
    CHECK(sb->free_blocks > 0 && sb->free_blocks < N_BLOCKS, "free_blocks sane");

    // The 16 MiB volume gets the 1 MiB journal floor (+1 JSB block).
    CHECK(sb->journal_blocks == (1u << 20) / BS + 1, "journal floor sizing");
}

static void corrupt_and_expect(u64 block, u32 byte_off, const char *what)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    make_bdev(&bdev, &state);
    CHECK(run_mkfs(&bdev) == CARA_EOK, "mkfs succeeds");

    volume[block * BS + byte_off] ^= 0x5A;
    u32 errs = fsck_errors(&bdev);
    if (errs == 0) {
        printf("FAIL: corruption not detected: %s\n", what);
        failures++;
    }
}

static void test_corruption_detection(void)
{
    // Re-derive the layout the same way mkfs does.
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    make_bdev(&bdev, &state);
    CHECK(run_mkfs(&bdev) == CARA_EOK, "mkfs succeeds");
    const struct CarafsSuperblock sb = *(const struct CarafsSuperblock *)volume;

    corrupt_and_expect(0, 4, "superblock magic byte");
    corrupt_and_expect(0, 100, "superblock body (crc)");
    corrupt_and_expect(sb.journal_start, 16, "JSB body");
    corrupt_and_expect(sb.ag_first_block, CARAFS_AG_BITS_OFF + 5, "AG bitmap bits");
    corrupt_and_expect(sb.ag_first_block, 16, "AG header free_count");
    corrupt_and_expect(sb.root_cnode, 40, "root cnode body");
    corrupt_and_expect(sb.backup_sb, 60, "backup superblock");
}

static void test_too_small(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    // 64 blocks = 256 KiB: smaller than the journal floor → ERANGE.
    Carafs_MemBdev_Init(&bdev, &state, volume, BS, 64);
    struct CarafsMkfsOpts opts = { .name = "X", .name_len = 1 };
    CHECK(Carafs_Mkfs(&bdev, &opts, scratch, BS) == CARA_ERANGE, "tiny device rejected");

    // Bad names rejected.
    Carafs_MemBdev_Init(&bdev, &state, volume, BS, N_BLOCKS);
    struct CarafsMkfsOpts bad = { .name = "a:b", .name_len = 3 };
    CHECK(Carafs_Mkfs(&bdev, &bad, scratch, BS) == CARA_EINVAL, "bad volume name rejected");
}

int main(void)
{
    volume = malloc(N_BLOCKS * BS);
    if (!volume) {
        return 1;
    }

    test_roundtrip();
    test_corruption_detection();
    test_too_small();

    free(volume);
    if (failures) {
        printf("test_carafs_mkfs: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_carafs_mkfs: ok\n");
    return 0;
}
