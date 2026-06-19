// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS F4: the metadata WAL (CARAFS.md §3.9/§4).
//
// Three layers of test:
//   1. replay_basic   — committed transactions survive a "crash" (a
//      remount with no intervening unmount); the journal replays them.
//   2. checkpoint_cycle — many transactions force checkpoints (log wrap)
//      and the volume stays consistent across them and a remount.
//   3. crash_injection — the §4 harness: a recording bdev captures every
//      block write of a mixed workload, then for EVERY prefix of that
//      write stream a fresh image is reconstructed, mounted (triggering
//      replay) and checked — fsck clean, no torn objects — modelling
//      power loss at each write boundary. The final prefix must recover
//      the complete committed state.

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

static u64 clock_ticks;
static u64 fake_now(void *ctx)
{
    (void)ctx;
    return ++clock_ticks * 1000000ull;
}

// Deterministic content for file i.
static u32 file_len(u32 i)
{
    return 100u + i * 500u; // i=0 inline; larger i spill to extents
}
static u8 file_byte(u32 i, u32 off)
{
    return (u8)(i * 131u + off * 7u + 17u);
}
static void fill_file(u8 *buf, u32 i)
{
    for (u32 o = 0; o < file_len(i); o++) {
        buf[o] = file_byte(i, o);
    }
}

static void mount_arena(struct CarafsMount *m, struct CarafsBdev *bdev, u8 *arena, usize abytes)
{
    struct CarafsMountOpts opts = { .cache_mem = arena, .cache_bytes = abytes, .now_ns = fake_now };
    CHECK(Carafs_Mount(m, bdev, &opts) == CARA_EOK, "mount");
}

static u32 fsck_errors(struct CarafsBdev *bdev, u8 *scratch)
{
    struct CarafsFsckReport rep;
    int rc = Carafs_Fsck(bdev, scratch, 2 * BS, &rep);
    CHECK(rc == CARA_EOK, "fsck runs");
    if (rep.errors) {
        printf("  fsck first error: %s @ block %llu\n", rep.first_error,
               (unsigned long long)rep.first_error_block);
    }
    return rep.errors;
}

// ---- 1. Committed transactions survive a crash (replay) ----------------------

static void test_replay_basic(void)
{
    constexpr u64 NB = (16ull << 20) / BS;
    u8 *vol = malloc(NB * BS);
    u8 *arena = malloc(1 << 20);
    u8 scratch[2 * BS];

    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    Carafs_MemBdev_Init(&bdev, &state, vol, BS, NB);
    struct CarafsMkfsOpts mo = { .name = "Work", .name_len = 4, .now_ns = 1000ull };
    CHECK(Carafs_Mkfs(&bdev, &mo, scratch, BS) == CARA_EOK, "mkfs");

    // Session A: create + write files, then "crash" (abandon the mount
    // with no unmount — committed data lives only in the cache + log).
    struct CarafsMount a;
    mount_arena(&a, &bdev, arena, 1 << 20);
    u64 root = a.sb.root_cnode;
    u8 buf[8192];
    char name[8];
    for (u32 i = 0; i < 8; i++) {
        snprintf(name, sizeof(name), "f%03u", i);
        u64 cn;
        CHECK(Carafs_DirCreate(&a, root, name, (u32)strlen(name), CARAFS_T_FILE, &cn) == CARA_EOK,
              "create");
        fill_file(buf, i);
        CHECK(Carafs_FileWrite(&a, cn, 0, buf, file_len(i)) == CARA_EOK, "write");
    }
    // No unmount. The on-disk superblock is DIRTY and the home blocks
    // may be stale — recovery must come from the journal.

    // Session B: a fresh mount over the same bytes must replay and see
    // everything committed in session A.
    struct CarafsMount b;
    u8 *arena2 = malloc(1 << 20);
    mount_arena(&b, &bdev, arena2, 1 << 20);
    CHECK(b.sb.state == CARAFS_STATE_CLEAN, "recovered mount is clean");
    for (u32 i = 0; i < 8; i++) {
        snprintf(name, sizeof(name), "f%03u", i);
        u64 cn;
        u16 ty;
        CHECK(Carafs_DirLookup(&b, root, name, (u32)strlen(name), &cn, &ty) == CARA_EOK,
              "survived create");
        usize got;
        u8 back[8192];
        CHECK(Carafs_FileRead(&b, cn, 0, back, file_len(i), &got) == CARA_EOK && got == file_len(i),
              "survived write len");
        bool ok = true;
        for (u32 o = 0; o < file_len(i); o++) {
            if (back[o] != file_byte(i, o)) {
                ok = false;
            }
        }
        CHECK(ok, "survived write content");
    }
    CHECK(Carafs_Unmount(&b) == CARA_EOK, "unmount B");
    CHECK(fsck_errors(&bdev, scratch) == 0, "fsck clean after replay");

    free(vol);
    free(arena);
    free(arena2);
}

// ---- 2. Checkpoints / log wrap -----------------------------------------------

static void test_checkpoint_cycle(void)
{
    constexpr u64 NB = (16ull << 20) / BS;
    u8 *vol = malloc(NB * BS);
    u8 *arena = malloc(1 << 20);
    u8 scratch[2 * BS];

    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    Carafs_MemBdev_Init(&bdev, &state, vol, BS, NB);
    struct CarafsMkfsOpts mo = { .name = "Work", .name_len = 4, .now_ns = 1000ull };
    CHECK(Carafs_Mkfs(&bdev, &mo, scratch, BS) == CARA_EOK, "mkfs");

    struct CarafsMount m;
    mount_arena(&m, &bdev, arena, 1 << 20);
    u64 root = m.sb.root_cnode;

    // Enough transactions to wrap the 1 MiB journal several times and
    // trip the half-full checkpoint repeatedly.
    constexpr u32 N = 3000;
    char name[12];
    for (u32 i = 0; i < N; i++) {
        snprintf(name, sizeof(name), "k%05u", i);
        u64 cn;
        CHECK(Carafs_DirCreate(&m, root, name, (u32)strlen(name), CARAFS_T_FILE, &cn) == CARA_EOK,
              "create many");
    }
    CHECK(m.stat_checkpoints > 0, "checkpoints actually fired");
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&bdev, scratch) == 0, "fsck clean after checkpoints");

    // Everything persists across a clean remount.
    struct CarafsMount m2;
    mount_arena(&m2, &bdev, arena, 1 << 20);
    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m2, root, &st) == CARA_EOK && st.size_bytes == N,
          "all entries persist");
    u64 cn;
    CHECK(Carafs_DirLookup(&m2, root, "k02999", 6, &cn, nullptr) == CARA_EOK, "last entry present");
    CHECK(Carafs_Unmount(&m2) == CARA_EOK, "unmount 2");

    free(vol);
    free(arena);
}

// ---- 3. Crash injection at every write boundary ------------------------------

struct Rec {
    u8 *live;
    u64 n_blocks;
    u32 bs;
    u32 nwrites;
    u32 cap;
    u64 *blk; // recorded target block per write
    u8 *data; // cap * bs recorded block images
    bool overflow;
};

static int rec_read(void *ctx, u64 block, u32 n, void *buf)
{
    struct Rec *r = ctx;
    if (block + n > r->n_blocks) {
        return CARA_ERANGE;
    }
    memcpy(buf, r->live + block * r->bs, (usize)n * r->bs);
    return CARA_EOK;
}
static int rec_write(void *ctx, u64 block, u32 n, const void *buf)
{
    struct Rec *r = ctx;
    if (block + n > r->n_blocks) {
        return CARA_ERANGE;
    }
    // The core only ever writes one block at a time.
    for (u32 i = 0; i < n; i++) {
        memcpy(r->live + (block + i) * r->bs, (const u8 *)buf + i * r->bs, r->bs);
        if (r->nwrites < r->cap) {
            r->blk[r->nwrites] = block + i;
            memcpy(r->data + (usize)r->nwrites * r->bs, (const u8 *)buf + i * r->bs, r->bs);
            r->nwrites++;
        } else {
            r->overflow = true;
        }
    }
    return CARA_EOK;
}
static int rec_flush(void *ctx)
{
    (void)ctx;
    return CARA_EOK;
}

// Verify a (possibly crash-recovered) image: fsck clean, and every
// listed file is whole — its size is either 0 (created, content write
// not yet committed) or its full length with the exact content. A
// partial size or wrong bytes is a torn-write bug.
static void verify_consistent(u8 *img, u64 nb, u8 *arena, usize abytes, u8 *scratch)
{
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
    Carafs_MemBdev_Init(&bdev, &state, img, BS, nb);

    struct CarafsMount m;
    struct CarafsMountOpts opts = { .cache_mem = arena, .cache_bytes = abytes, .now_ns = fake_now };
    int rc = Carafs_Mount(&m, &bdev, &opts);
    CHECK(rc == CARA_EOK, "crash image mounts");
    if (rc != CARA_EOK) {
        return;
    }
    u64 root = m.sb.root_cnode;

    struct CarafsDirCursor cur = { 0 };
    struct CarafsDirEntry ent;
    u8 back[8192];
    while (Carafs_DirNext(&m, root, &cur, &ent) == CARA_EOK) {
        if (ent.type != CARAFS_T_FILE || ent.name_len != 4 || ent.name[0] != 'f') {
            continue; // "sub" directory or other
        }
        u32 i = (u32)((ent.name[1] - '0') * 100 + (ent.name[2] - '0') * 10 + (ent.name[3] - '0'));
        struct CarafsStat st;
        CHECK(Carafs_CnodeStat(&m, ent.cnode, &st) == CARA_EOK, "stat listed file");
        CHECK(st.size_bytes == 0 || st.size_bytes == file_len(i), "file size whole (0 or full)");
        if (st.size_bytes == file_len(i)) {
            usize got;
            CHECK(Carafs_FileRead(&m, ent.cnode, 0, back, file_len(i), &got) == CARA_EOK &&
                      got == file_len(i),
                  "read whole file");
            bool ok = true;
            for (u32 o = 0; o < file_len(i); o++) {
                if (back[o] != file_byte(i, o)) {
                    ok = false;
                }
            }
            CHECK(ok, "committed content intact (no torn write)");
        }
    }
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount crash image");
    CHECK(fsck_errors(&bdev, scratch) == 0, "crash image fsck clean");
}

static void test_crash_injection(void)
{
    constexpr u64 NB = (8ull << 20) / BS;
    usize vbytes = NB * BS;
    u8 *base = malloc(vbytes);
    u8 *live = malloc(vbytes);
    u8 *crashed = malloc(vbytes);
    u8 *arena = malloc(1 << 20);
    u8 scratch[2 * BS];
    constexpr u32 CAP = 8192;
    u64 *rblk = malloc(CAP * sizeof(u64));
    u8 *rdata = malloc((usize)CAP * BS);

    // Pristine post-mkfs image → base.
    {
        struct CarafsMemBdev state;
        struct CarafsBdev bdev;
        Carafs_MemBdev_Init(&bdev, &state, base, BS, NB);
        struct CarafsMkfsOpts mo = { .name = "Work", .name_len = 4, .now_ns = 1000ull };
        CHECK(Carafs_Mkfs(&bdev, &mo, scratch, BS) == CARA_EOK, "mkfs base");
    }

    // Run a mixed workload on a recording bdev, capturing every write.
    memcpy(live, base, vbytes);
    struct Rec rec = {
        .live = live, .n_blocks = NB, .bs = BS, .cap = CAP, .blk = rblk, .data = rdata
    };
    struct CarafsBdev rbdev = { .ctx = &rec,
                                .block_size = BS,
                                .n_blocks = NB,
                                .read = rec_read,
                                .write = rec_write,
                                .flush = rec_flush };
    struct CarafsMount m;
    mount_arena(&m, &rbdev, arena, 1 << 20);
    u64 root = m.sb.root_cnode;

    constexpr u32 NF = 12;
    u8 buf[8192];
    char name[8];
    u64 sub;
    CHECK(Carafs_DirCreate(&m, root, "sub", 3, CARAFS_T_DIR, &sub) == CARA_EOK, "mkdir sub");
    for (u32 i = 0; i < NF; i++) {
        snprintf(name, sizeof(name), "f%03u", i);
        u64 cn;
        CHECK(Carafs_DirCreate(&m, root, name, (u32)strlen(name), CARAFS_T_FILE, &cn) == CARA_EOK,
              "wl create");
        fill_file(buf, i);
        CHECK(Carafs_FileWrite(&m, cn, 0, buf, file_len(i)) == CARA_EOK, "wl write");
    }
    // A few removals exercise free + tombstone transactions.
    for (u32 i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "f%03u", i * 2);
        CHECK(Carafs_DirRemove(&m, root, name, (u32)strlen(name)) == CARA_EOK, "wl remove");
    }
    // Crash: abandon the mount (no unmount). rec.nwrites now holds the
    // full ordered write stream of the workload.
    CHECK(!rec.overflow, "write record did not overflow");
    printf("  crash-injection: %u recorded writes\n", rec.nwrites);

    // For every prefix of the write stream, reconstruct base + writes
    // and assert the recovered volume is consistent.
    for (u32 k = 0; k <= rec.nwrites; k++) {
        memcpy(crashed, base, vbytes);
        for (u32 i = 0; i < k; i++) {
            memcpy(crashed + rblk[i] * BS, rdata + (usize)i * BS, BS);
        }
        verify_consistent(crashed, NB, arena, 1 << 20, scratch);
    }

    // The full stream must recover the complete committed state: the
    // surviving files present and whole, the removed ones gone.
    memcpy(crashed, base, vbytes);
    for (u32 i = 0; i < rec.nwrites; i++) {
        memcpy(crashed + rblk[i] * BS, rdata + (usize)i * BS, BS);
    }
    {
        struct CarafsMemBdev state;
        struct CarafsBdev bdev;
        Carafs_MemBdev_Init(&bdev, &state, crashed, BS, NB);
        struct CarafsMount fm;
        mount_arena(&fm, &bdev, arena, 1 << 20);
        u64 froot = fm.sb.root_cnode;
        u64 fcn;
        for (u32 i = 0; i < NF; i++) {
            snprintf(name, sizeof(name), "f%03u", i);
            int r = Carafs_DirLookup(&fm, froot, name, (u32)strlen(name), &fcn, nullptr);
            bool removed = (i % 2 == 0) && (i < 8);
            if (removed) {
                CHECK(r == CARA_ENOENT, "removed file absent in final state");
            } else {
                CHECK(r == CARA_EOK, "surviving file present in final state");
                usize got;
                CHECK(Carafs_FileRead(&fm, fcn, 0, buf, file_len(i), &got) == CARA_EOK &&
                          got == file_len(i),
                      "final content length");
            }
        }
        CHECK(Carafs_DirLookup(&fm, froot, "sub", 3, &fcn, nullptr) == CARA_EOK,
              "subdir present in final state");
        CHECK(Carafs_Unmount(&fm) == CARA_EOK, "unmount final");
        CHECK(fsck_errors(&bdev, scratch) == 0, "final fsck clean");
    }

    free(base);
    free(live);
    free(crashed);
    free(arena);
    free(rblk);
    free(rdata);
}

int main(void)
{
    test_replay_basic();
    test_checkpoint_cycle();
    test_crash_injection();

    if (failures) {
        printf("test_carafs_journal: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_carafs_journal: ok\n");
    return 0;
}
