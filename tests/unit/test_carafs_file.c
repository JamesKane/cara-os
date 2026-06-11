// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS F2: mount/unmount, cnode lifecycle, and file content on a
// memory bdev — inline data, promotion to extents, sparse holes,
// append fragmentation deep enough to force the extent B+tree
// through leaf splits, write-into-hole, and the free-space
// accounting invariant (delete returns every block; fsck cross-checks
// the AG popcounts and the superblock total on the unmounted image).

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
static u8 arena[512 * 1024];

static u64 clock_ticks;
static u64 fake_now(void *ctx)
{
    (void)ctx;
    return ++clock_ticks * 1000000ull;
}

static void make_bdev(struct CarafsBdev *bdev, struct CarafsMemBdev *state)
{
    Carafs_MemBdev_Init(bdev, state, volume, BS, N_BLOCKS);
}

static void fresh_volume(struct CarafsBdev *bdev, struct CarafsMemBdev *state)
{
    make_bdev(bdev, state);
    struct CarafsMkfsOpts opts = { .name = "Work", .name_len = 4, .now_ns = 1000ull };
    int rc = Carafs_Mkfs(bdev, &opts, scratch, BS);
    CHECK(rc == CARA_EOK, "mkfs succeeds");
}

static void mount_vol(struct CarafsMount *m, struct CarafsBdev *bdev, bool readonly)
{
    struct CarafsMountOpts opts = {
        .cache_mem = arena,
        .cache_bytes = sizeof(arena),
        .readonly = readonly,
        .now_ns = fake_now,
    };
    int rc = Carafs_Mount(m, bdev, &opts);
    CHECK(rc == CARA_EOK, "mount succeeds");
}

static u32 fsck_errors(struct CarafsBdev *bdev)
{
    struct CarafsFsckReport rep;
    int rc = Carafs_Fsck(bdev, scratch, sizeof(scratch), &rep);
    CHECK(rc == CARA_EOK, "fsck must be able to run");
    if (rep.errors) {
        printf("  fsck first error: %s @ block %llu\n", rep.first_error,
               (unsigned long long)rep.first_error_block);
    }
    return rep.errors;
}

static u8 pat(u32 seed, u64 i)
{
    return (u8)(((u64)seed * 131 + i) * 2654435761ull >> 7);
}

static void fill_pat(u8 *buf, usize len, u32 seed, u64 base)
{
    for (usize i = 0; i < len; i++) {
        buf[i] = pat(seed, base + i);
    }
}

static bool check_pat(const u8 *buf, usize len, u32 seed, u64 base)
{
    for (usize i = 0; i < len; i++) {
        if (buf[i] != pat(seed, base + i)) {
            return false;
        }
    }
    return true;
}

static bool all_zero(const u8 *buf, usize len)
{
    for (usize i = 0; i < len; i++) {
        if (buf[i]) {
            return false;
        }
    }
    return true;
}

// Read the whole file in 64 KiB chunks and verify the pattern.
static bool verify_file(struct CarafsMount *m, u64 cnode, u64 size, u32 seed)
{
    static u8 chunk[64 * 1024];
    for (u64 off = 0; off < size; off += sizeof(chunk)) {
        usize want = (usize)(size - off < sizeof(chunk) ? size - off : sizeof(chunk));
        usize got;
        if (Carafs_FileRead(m, cnode, off, chunk, want, &got) != CARA_EOK || got != want) {
            return false;
        }
        if (!check_pat(chunk, got, seed, off)) {
            return false;
        }
    }
    return true;
}

static void test_mount_basics(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    struct CarafsMount m;
    fresh_volume(&bdev, &state);

    // Bad magic refused.
    volume[0] ^= 0x5A;
    struct CarafsMountOpts mo = { .cache_mem = arena, .cache_bytes = sizeof(arena) };
    CHECK(Carafs_Mount(&m, &bdev, &mo) == CARA_EBADMAGIC, "corrupt sb refused");
    volume[0] ^= 0x5A;

    // Arena too small refused.
    struct CarafsMountOpts tiny = { .cache_mem = arena, .cache_bytes = 8 * BS };
    CHECK(Carafs_Mount(&m, &bdev, &tiny) == CARA_ENOMEM, "tiny arena refused");

    mount_vol(&m, &bdev, false);
    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, m.sb.root_cnode, &st) == CARA_EOK, "stat root");
    CHECK(st.type == CARAFS_T_DIR && st.link_count == 1, "root is a directory");
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&bdev) == 0, "clean after mount/unmount");

    // A pure read-only mount writes nothing and rejects mutation.
    mount_vol(&m, &bdev, true);
    u64 cn;
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &cn) == CARA_EINVAL, "ro create rejected");
    CHECK(Carafs_FileWrite(&m, m.sb.root_cnode, 0, "x", 1) == CARA_EINVAL, "ro write rejected");
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "ro unmount");
    CHECK(m.stat_bdev_writes == 0, "ro mount never wrote");
}

static void test_inline_data(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    struct CarafsMount m;
    fresh_volume(&bdev, &state);
    mount_vol(&m, &bdev, false);

    u64 cn;
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &cn) == CARA_EOK, "create file");

    u8 buf[2000];
    fill_pat(buf, 100, 7, 0);
    CHECK(Carafs_FileWrite(&m, cn, 0, buf, 100) == CARA_EOK, "small write");

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, cn, &st) == CARA_EOK, "stat");
    CHECK(st.size_bytes == 100, "size 100");
    CHECK(st.blocks_used == 0, "tiny file stays inline");

    // Sparse inline extension: bytes 100..1000 read as zeros.
    fill_pat(buf, 1000, 7, 1000);
    CHECK(Carafs_FileWrite(&m, cn, 1000, buf, 1000) == CARA_EOK, "inline extend");
    usize got;
    u8 back[2000];
    CHECK(Carafs_FileRead(&m, cn, 0, back, sizeof(back), &got) == CARA_EOK && got == 2000,
          "read inline file");
    CHECK(check_pat(back, 100, 7, 0), "head data");
    CHECK(all_zero(back + 100, 900), "inline gap is zeros");
    CHECK(check_pat(back + 1000, 1000, 7, 1000), "tail data");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&bdev) == 0, "fsck clean");

    // Persistence across remount.
    mount_vol(&m, &bdev, false);
    CHECK(Carafs_FileRead(&m, cn, 0, back, sizeof(back), &got) == CARA_EOK && got == 2000,
          "reread after remount");
    CHECK(check_pat(back + 1000, 1000, 7, 1000), "data survived remount");
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount 2");
}

static void test_promotion(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    struct CarafsMount m;
    fresh_volume(&bdev, &state);
    mount_vol(&m, &bdev, false);

    u64 cn;
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &cn) == CARA_EOK, "create file");

    // Seed inline, then grow past the item area → promotion.
    u8 *buf = malloc(12000);
    fill_pat(buf, 2000, 9, 0);
    CHECK(Carafs_FileWrite(&m, cn, 0, buf, 2000) == CARA_EOK, "inline seed");
    fill_pat(buf, 12000, 9, 0);
    CHECK(Carafs_FileWrite(&m, cn, 0, buf, 12000) == CARA_EOK, "promoting write");

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, cn, &st) == CARA_EOK, "stat");
    CHECK(st.size_bytes == 12000, "promoted size");
    CHECK(st.blocks_used == 3, "12000 B = 3 blocks");
    CHECK(verify_file(&m, cn, 12000, 9), "promoted content");

    // Partial overwrite in the middle (read-modify-write path).
    fill_pat(buf, 100, 9, 5000);
    CHECK(Carafs_FileWrite(&m, cn, 5000, buf, 100) == CARA_EOK, "partial overwrite");
    CHECK(verify_file(&m, cn, 12000, 9), "content after overwrite");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&bdev) == 0, "fsck clean");
    mount_vol(&m, &bdev, false);
    CHECK(verify_file(&m, cn, 12000, 9), "content after remount");
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount 2");
    free(buf);
}

static void test_sparse(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    struct CarafsMount m;
    fresh_volume(&bdev, &state);
    mount_vol(&m, &bdev, false);

    u64 cn;
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &cn) == CARA_EOK, "create file");

    u8 blk[BS];
    fill_pat(blk, BS, 11, 0);
    CHECK(Carafs_FileWrite(&m, cn, 0, blk, BS) == CARA_EOK, "head block");
    u64 far = 8ull << 20; // 8 MiB
    fill_pat(blk, BS, 11, far);
    CHECK(Carafs_FileWrite(&m, cn, far, blk, BS) == CARA_EOK, "far block");

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, cn, &st) == CARA_EOK, "stat");
    CHECK(st.size_bytes == far + BS, "sparse size");
    CHECK(st.blocks_used == 2, "only two blocks allocated");

    u8 back[BS];
    usize got;
    CHECK(Carafs_FileRead(&m, cn, 0, back, BS, &got) == CARA_EOK && got == BS, "read head");
    CHECK(check_pat(back, BS, 11, 0), "head content");
    CHECK(Carafs_FileRead(&m, cn, 4ull << 20, back, BS, &got) == CARA_EOK && got == BS,
          "read hole");
    CHECK(all_zero(back, BS), "hole reads as zeros");
    CHECK(Carafs_FileRead(&m, cn, far, back, BS, &got) == CARA_EOK && got == BS, "read far");
    CHECK(check_pat(back, BS, 11, far), "far content");
    // Reads straddling EOF clamp.
    CHECK(Carafs_FileRead(&m, cn, far + BS - 10, back, BS, &got) == CARA_EOK && got == 10,
          "EOF clamp");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&bdev) == 0, "fsck clean");
}

// Interleaved appends fragment both files (the rotor alternates
// between them), pushing each past 16 extents (tree spill) and past
// one leaf's 169 records (leaf split + root growth).
static void test_append_fragmentation(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    struct CarafsMount m;
    fresh_volume(&bdev, &state);
    mount_vol(&m, &bdev, false);

    constexpr u32 APPENDS = 200;
    u64 f1, f2;
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &f1) == CARA_EOK, "create f1");
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &f2) == CARA_EOK, "create f2");

    u8 blk[BS];
    for (u32 i = 0; i < APPENDS; i++) {
        u64 off = (u64)i * BS;
        fill_pat(blk, BS, 21, off);
        CHECK(Carafs_FileWrite(&m, f1, off, blk, BS) == CARA_EOK, "append f1");
        fill_pat(blk, BS, 22, off);
        CHECK(Carafs_FileWrite(&m, f2, off, blk, BS) == CARA_EOK, "append f2");
    }

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, f1, &st) == CARA_EOK, "stat f1");
    CHECK(st.size_bytes == (u64)APPENDS * BS, "f1 size");
    CHECK(st.blocks_used == APPENDS, "f1 blocks_used");
    CHECK(verify_file(&m, f1, (u64)APPENDS * BS, 21), "f1 content");
    CHECK(verify_file(&m, f2, (u64)APPENDS * BS, 22), "f2 content");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&bdev) == 0, "fsck clean");

    // Structural proof the inline array spilled: both cnodes must
    // carry an extent tree on disk (200 interleaved extents >> 16
    // inline slots and > one 169-record leaf).
    const struct CarafsCnode *cn1 = (const struct CarafsCnode *)(volume + f1 * BS);
    const struct CarafsCnode *cn2 = (const struct CarafsCnode *)(volume + f2 * BS);
    CHECK(cn1->tree_root != 0 && cn1->n_inline_extents == 0, "f1 spilled to a tree");
    CHECK(cn2->tree_root != 0 && cn2->n_inline_extents == 0, "f2 spilled to a tree");

    mount_vol(&m, &bdev, false);
    CHECK(verify_file(&m, f1, (u64)APPENDS * BS, 21), "f1 after remount");
    CHECK(verify_file(&m, f2, (u64)APPENDS * BS, 22), "f2 after remount");

    // Cleanup must return every block (tree nodes included).
    u64 free_before_delete = m.sb.free_blocks;
    CHECK(Carafs_CnodeDelete(&m, f1) == CARA_EOK, "delete f1");
    CHECK(Carafs_CnodeDelete(&m, f2) == CARA_EOK, "delete f2");
    CHECK(m.sb.free_blocks > free_before_delete + 2 * APPENDS, "tree nodes freed too");
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount 2");
    CHECK(fsck_errors(&bdev) == 0, "fsck clean after delete");
}

static void test_write_into_hole(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    struct CarafsMount m;
    fresh_volume(&bdev, &state);
    mount_vol(&m, &bdev, false);

    u64 cn;
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &cn) == CARA_EOK, "create file");

    u8 blk[BS];
    fill_pat(blk, BS, 31, 0);
    CHECK(Carafs_FileWrite(&m, cn, 0, blk, BS) == CARA_EOK, "block 0");
    fill_pat(blk, BS, 31, 5ull * BS);
    CHECK(Carafs_FileWrite(&m, cn, 5ull * BS, blk, BS) == CARA_EOK, "block 5");

    // Fill part of the hole — a partial write into a fresh block must
    // keep the rest of that block zero.
    u8 tiny[100];
    fill_pat(tiny, 100, 31, 2ull * BS + 50);
    CHECK(Carafs_FileWrite(&m, cn, 2ull * BS + 50, tiny, 100) == CARA_EOK, "fill hole");

    u8 back[BS];
    usize got;
    CHECK(Carafs_FileRead(&m, cn, 2ull * BS, back, BS, &got) == CARA_EOK && got == BS,
          "read filled block");
    CHECK(all_zero(back, 50), "pre-pad zero");
    CHECK(check_pat(back + 50, 100, 31, 2ull * BS + 50), "filled data");
    CHECK(all_zero(back + 150, BS - 150), "post-pad zero");
    CHECK(Carafs_FileRead(&m, cn, BS, back, BS, &got) == CARA_EOK, "neighbour hole");
    CHECK(all_zero(back, BS), "block 1 still a hole");

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, cn, &st) == CARA_EOK, "stat");
    CHECK(st.blocks_used == 3, "three data blocks");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&bdev) == 0, "fsck clean");
}

static void test_delete_accounting(void)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    struct CarafsMount m;
    fresh_volume(&bdev, &state);
    mount_vol(&m, &bdev, false);

    u64 baseline = m.sb.free_blocks;

    // One of each shape: inline, plain extents, sparse.
    u64 a, b, c;
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &a) == CARA_EOK, "create a");
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &b) == CARA_EOK, "create b");
    CHECK(Carafs_CnodeCreate(&m, CARAFS_T_FILE, 0, &c) == CARA_EOK, "create c");
    u8 *buf = malloc(64 * 1024);
    fill_pat(buf, 500, 41, 0);
    CHECK(Carafs_FileWrite(&m, a, 0, buf, 500) == CARA_EOK, "inline file");
    fill_pat(buf, 64 * 1024, 42, 0);
    CHECK(Carafs_FileWrite(&m, b, 0, buf, 64 * 1024) == CARA_EOK, "extent file");
    fill_pat(buf, BS, 43, 0);
    CHECK(Carafs_FileWrite(&m, c, 2ull << 20, buf, BS) == CARA_EOK, "sparse file");
    free(buf);

    CHECK(m.sb.free_blocks < baseline, "blocks consumed");

    CHECK(Carafs_CnodeDelete(&m, a) == CARA_EOK, "delete a");
    CHECK(Carafs_CnodeDelete(&m, b) == CARA_EOK, "delete b");
    CHECK(Carafs_CnodeDelete(&m, c) == CARA_EOK, "delete c");
    CHECK(m.sb.free_blocks == baseline, "every block returned");

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, a, &st) == CARA_ENOENT, "deleted cnode is a tombstone");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&bdev) == 0, "fsck clean — AG counts + sb total agree");
}

int main(void)
{
    volume = malloc(N_BLOCKS * BS);
    if (!volume) {
        return 1;
    }

    test_mount_basics();
    test_inline_data();
    test_promotion();
    test_sparse();
    test_append_fragmentation();
    test_write_into_hole();
    test_delete_accounting();

    free(volume);
    if (failures) {
        printf("test_carafs_file: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_carafs_file: ok\n");
    return 0;
}
