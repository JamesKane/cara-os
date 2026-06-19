// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS F3: the directory layer on a memory bdev — create/lookup/
// remove with case-insensitive folding, the inline -> single-leaf ->
// B+tree promotion under load, key-order iteration, hard and soft
// links with reference counting, nested directories with parent
// linkage and the empty-directory rule, and the million-entry scale
// test that asserts lookup cost stays logarithmic (block reads counted,
// not wall time, per CARAFS.md §3.6). fsck cross-checks the directory
// structure on the smaller trees.

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

struct Vol {
    u8 *mem;
    u64 n_blocks;
    u8 *arena;
    usize arena_bytes;
    struct CarafsMemBdev state;
    struct CarafsBdev bdev;
};

static void vol_make(struct Vol *v, u64 bytes, usize arena_bytes)
{
    v->n_blocks = bytes / BS;
    v->mem = malloc(bytes);
    v->arena = malloc(arena_bytes);
    v->arena_bytes = arena_bytes;
    Carafs_MemBdev_Init(&v->bdev, &v->state, v->mem, BS, v->n_blocks);
    struct CarafsMkfsOpts opts = { .name = "Work", .name_len = 4, .now_ns = 1000ull };
    u8 scratch[BS];
    CHECK(Carafs_Mkfs(&v->bdev, &opts, scratch, BS) == CARA_EOK, "mkfs");
}

static void vol_free(struct Vol *v)
{
    free(v->mem);
    free(v->arena);
}

static void mount_vol(struct CarafsMount *m, struct Vol *v)
{
    struct CarafsMountOpts opts = { .cache_mem = v->arena,
                                    .cache_bytes = v->arena_bytes,
                                    .now_ns = fake_now };
    CHECK(Carafs_Mount(m, &v->bdev, &opts) == CARA_EOK, "mount");
}

static u32 fsck_errors(struct Vol *v)
{
    static u8 scratch[2 * BS];
    struct CarafsFsckReport rep;
    int rc = Carafs_Fsck(&v->bdev, scratch, sizeof(scratch), &rep);
    CHECK(rc == CARA_EOK, "fsck runs");
    if (rep.errors) {
        printf("  fsck first error: %s @ block %llu\n", rep.first_error,
               (unsigned long long)rep.first_error_block);
    }
    return rep.errors;
}

// ---- Tests --------------------------------------------------------------------

static void test_basic(void)
{
    struct Vol v;
    vol_make(&v, 16ull << 20, 512 * 1024);
    struct CarafsMount m;
    mount_vol(&m, &v);
    u64 root = m.sb.root_cnode;

    u64 a, b;
    CHECK(Carafs_DirCreate(&m, root, "Hello", 5, CARAFS_T_FILE, &a) == CARA_EOK, "create Hello");
    CHECK(Carafs_DirCreate(&m, root, "World", 5, CARAFS_T_DIR, &b) == CARA_EOK, "create World");

    // Duplicate (any case) is refused.
    u64 dup;
    CHECK(Carafs_DirCreate(&m, root, "hello", 5, CARAFS_T_FILE, &dup) == CARA_EEXIST,
          "dup refused");

    // Case-insensitive lookup, case-preserving storage.
    u64 cn;
    u16 ty;
    CHECK(Carafs_DirLookup(&m, root, "HELLO", 5, &cn, &ty) == CARA_EOK && cn == a &&
              ty == CARAFS_T_FILE,
          "fold lookup Hello");
    CHECK(Carafs_DirLookup(&m, root, "world", 5, &cn, &ty) == CARA_EOK && cn == b &&
              ty == CARAFS_T_DIR,
          "fold lookup World");
    CHECK(Carafs_DirLookup(&m, root, "nope", 4, &cn, &ty) == CARA_ENOENT, "missing → ENOENT");

    // Root entry count tracks links.
    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, root, &st) == CARA_EOK && st.size_bytes == 2, "root has 2 entries");

    // Child carries its parent and is freed on removal.
    CHECK(Carafs_CnodeStat(&m, b, &st) == CARA_EOK && st.parent_cnode == root, "subdir parent");
    CHECK(Carafs_DirRemove(&m, root, "Hello", 5) == CARA_EOK, "remove Hello");
    CHECK(Carafs_DirLookup(&m, root, "Hello", 5, &cn, &ty) == CARA_ENOENT, "Hello gone");
    CHECK(Carafs_CnodeStat(&m, a, &st) == CARA_ENOENT, "Hello cnode tombstoned");
    CHECK(Carafs_DirRemove(&m, root, "missing", 7) == CARA_ENOENT, "remove missing");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&v) == 0, "fsck clean");
    vol_free(&v);
}

// Build N entries to force inline → leaf → tree, verify every lookup,
// iterate in key order, then delete half and re-verify.
static void test_promotion(void)
{
    struct Vol v;
    vol_make(&v, 64ull << 20, 4 * 1024 * 1024);
    struct CarafsMount m;
    mount_vol(&m, &v);
    u64 root = m.sb.root_cnode;

    constexpr u32 N = 2000;
    char name[32];
    for (u32 i = 0; i < N; i++) {
        snprintf(name, sizeof(name), "file%06u", i);
        u64 cn;
        CHECK(Carafs_DirCreate(&m, root, name, (u32)strlen(name), CARAFS_T_FILE, &cn) == CARA_EOK,
              "create N");
    }

    // The directory must have outgrown the inline item into a B+tree.
    const struct CarafsCnode *rootcn = (const struct CarafsCnode *)(v.mem + root * BS);
    CHECK(rootcn->tree_root != 0, "root promoted to a tree");
    const struct CarafsBtNode *rootnode =
        (const struct CarafsBtNode *)(v.mem + rootcn->tree_root * BS);
    CHECK(rootnode->magic == CARAFS_MAGIC_BTREE && rootnode->flavour == CARAFS_BT_DIR,
          "dir-flavoured tree");
    CHECK(rootnode->level >= 1, "multi-level tree");

    // Every name resolves.
    for (u32 i = 0; i < N; i++) {
        snprintf(name, sizeof(name), "file%06u", i);
        u64 cn;
        CHECK(Carafs_DirLookup(&m, root, name, (u32)strlen(name), &cn, nullptr) == CARA_EOK,
              "lookup each");
    }

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, root, &st) == CARA_EOK && st.size_bytes == N, "entry count");

    // Iterate: exactly N entries, strictly ascending key order.
    struct CarafsDirCursor cur = { 0 };
    struct CarafsDirEntry ent;
    u32 seen = 0;
    u64 prev_hash = 0;
    u8 prev_seq = 0;
    bool ordered = true;
    int rc;
    while ((rc = Carafs_DirNext(&m, root, &cur, &ent)) == CARA_EOK) {
        if (seen > 0) {
            if (cur.hash < prev_hash || (cur.hash == prev_hash && cur.seq <= prev_seq)) {
                ordered = false;
            }
        }
        prev_hash = cur.hash;
        prev_seq = cur.seq;
        seen++;
    }
    CHECK(rc == CARA_ENOENT, "iteration ends with ENOENT");
    CHECK(seen == N, "iterated every entry");
    CHECK(ordered, "iteration is key-ordered");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&v) == 0, "fsck clean after build");

    // Persistence + deletion of half.
    mount_vol(&m, &v);
    u64 free_before = m.sb.free_blocks;
    for (u32 i = 0; i < N; i += 2) {
        snprintf(name, sizeof(name), "file%06u", i);
        CHECK(Carafs_DirRemove(&m, root, name, (u32)strlen(name)) == CARA_EOK, "remove evens");
    }
    CHECK(m.sb.free_blocks > free_before, "deletion returned blocks");
    CHECK(Carafs_CnodeStat(&m, root, &st) == CARA_EOK && st.size_bytes == N / 2, "half remain");
    for (u32 i = 0; i < N; i++) {
        snprintf(name, sizeof(name), "file%06u", i);
        u64 cn;
        int r = Carafs_DirLookup(&m, root, name, (u32)strlen(name), &cn, nullptr);
        CHECK(r == ((i & 1) ? CARA_EOK : CARA_ENOENT), "odds survive, evens gone");
    }

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount 2");
    CHECK(fsck_errors(&v) == 0, "fsck clean after delete");
    vol_free(&v);
}

static void test_links(void)
{
    struct Vol v;
    vol_make(&v, 16ull << 20, 512 * 1024);
    struct CarafsMount m;
    mount_vol(&m, &v);
    u64 root = m.sb.root_cnode;

    // Hard links: one cnode, two names, shared content.
    u64 file;
    CHECK(Carafs_DirCreate(&m, root, "orig", 4, CARAFS_T_FILE, &file) == CARA_EOK, "create orig");
    const char *payload = "shared bytes";
    CHECK(Carafs_FileWrite(&m, file, 0, payload, 12) == CARA_EOK, "write via orig");
    CHECK(Carafs_DirLink(&m, root, "alias", 5, file) == CARA_EOK, "hard link alias");

    u64 cn;
    CHECK(Carafs_DirLookup(&m, root, "alias", 5, &cn, nullptr) == CARA_EOK && cn == file,
          "alias → same cnode");
    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, file, &st) == CARA_EOK && st.link_count == 2, "link_count 2");

    u8 back[16];
    usize got;
    CHECK(Carafs_FileRead(&m, file, 0, back, 12, &got) == CARA_EOK && got == 12 &&
              memcmp(back, payload, 12) == 0,
          "content via cnode");

    // Hard-linking a directory is refused.
    u64 dir;
    CHECK(Carafs_DirCreate(&m, root, "adir", 4, CARAFS_T_DIR, &dir) == CARA_EOK, "create adir");
    CHECK(Carafs_DirLink(&m, root, "dlink", 5, dir) == CARA_EINVAL, "no dir hard links");

    // Removing one name drops the count; the object survives.
    CHECK(Carafs_DirRemove(&m, root, "orig", 4) == CARA_EOK, "remove orig");
    CHECK(Carafs_CnodeStat(&m, file, &st) == CARA_EOK && st.link_count == 1, "link_count 1");
    CHECK(Carafs_FileRead(&m, file, 0, back, 12, &got) == CARA_EOK && got == 12, "still readable");
    // Removing the last name frees it.
    CHECK(Carafs_DirRemove(&m, root, "alias", 5) == CARA_EOK, "remove alias");
    CHECK(Carafs_CnodeStat(&m, file, &st) == CARA_ENOENT, "freed at zero links");

    // Soft links: a SYMLINK cnode carrying a target path.
    u64 sym;
    const char *target = "Work:foo/bar";
    CHECK(Carafs_DirSymlink(&m, root, "lnk", 3, target, 12, &sym) == CARA_EOK, "symlink");
    u16 ty;
    CHECK(Carafs_DirLookup(&m, root, "lnk", 3, &cn, &ty) == CARA_EOK && cn == sym &&
              ty == CARAFS_T_SYMLINK,
          "symlink lookup");
    char tbuf[32];
    usize tlen;
    CHECK(Carafs_SymlinkRead(&m, sym, tbuf, sizeof(tbuf), &tlen) == CARA_EOK && tlen == 12 &&
              memcmp(tbuf, target, 12) == 0,
          "symlink target");
    CHECK(Carafs_SymlinkRead(&m, dir, tbuf, sizeof(tbuf), &tlen) == CARA_EINVAL,
          "symlink read on a non-symlink");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&v) == 0, "fsck clean");
    vol_free(&v);
}

static void test_nested(void)
{
    struct Vol v;
    vol_make(&v, 16ull << 20, 512 * 1024);
    struct CarafsMount m;
    mount_vol(&m, &v);
    u64 root = m.sb.root_cnode;

    u64 sub, child;
    CHECK(Carafs_DirCreate(&m, root, "Sub", 3, CARAFS_T_DIR, &sub) == CARA_EOK, "mkdir Sub");
    CHECK(Carafs_DirCreate(&m, sub, "inner", 5, CARAFS_T_FILE, &child) == CARA_EOK, "file in Sub");

    // Manual two-level path resolution.
    u64 cn;
    CHECK(Carafs_DirLookup(&m, root, "Sub", 3, &cn, nullptr) == CARA_EOK && cn == sub, "find Sub");
    CHECK(Carafs_DirLookup(&m, cn, "inner", 5, &cn, nullptr) == CARA_EOK && cn == child,
          "find inner under Sub");

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, child, &st) == CARA_EOK && st.parent_cnode == sub, "inner parent");

    // A non-empty directory cannot be removed.
    CHECK(Carafs_DirRemove(&m, root, "Sub", 3) == CARA_ENOTEMPTY, "non-empty dir refused");
    CHECK(Carafs_DirRemove(&m, sub, "inner", 5) == CARA_EOK, "empty it");
    CHECK(Carafs_DirRemove(&m, root, "Sub", 3) == CARA_EOK, "now removable");
    CHECK(Carafs_CnodeStat(&m, sub, &st) == CARA_ENOENT, "Sub freed");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&v) == 0, "fsck clean");
    vol_free(&v);
}

// A large directory built via hard links to one anchor cnode (so the
// cost is directory-tree storage, not N × 4 KiB cnodes). Asserts lookup
// stays logarithmic by counting block reads, per CARAFS.md §3.6. N is
// big enough to force a three-level tree; it is kept below 10^6 because
// each link is now its own journalled transaction (F4) — the property
// under test is lookup depth, not insert throughput.
static void test_scale(void)
{
    struct Vol v;
    vol_make(&v, 64ull << 20, 8ull << 20);
    struct CarafsMount m;
    mount_vol(&m, &v);
    u64 root = m.sb.root_cnode;

    u64 anchor;
    CHECK(Carafs_DirCreate(&m, root, "anchor", 6, CARAFS_T_FILE, &anchor) == CARA_EOK, "anchor");

    constexpr u32 N = 200000;
    char name[24];
    for (u32 i = 0; i < N; i++) {
        snprintf(name, sizeof(name), "L%07u", i);
        if (Carafs_DirLink(&m, root, name, (u32)strlen(name), anchor) != CARA_EOK) {
            CHECK(false, "hard link at scale");
            break;
        }
    }

    struct CarafsStat st;
    CHECK(Carafs_CnodeStat(&m, root, &st) == CARA_EOK && st.size_bytes == (u64)N + 1,
          "million+anchor entries");
    CHECK(Carafs_CnodeStat(&m, anchor, &st) == CARA_EOK && st.link_count == (u32)N + 1,
          "anchor link_count");

    // A deep, multi-level tree exists on disk.
    const struct CarafsCnode *rootcn = (const struct CarafsCnode *)(v.mem + root * BS);
    CHECK(rootcn->tree_root != 0, "tree present");
    const struct CarafsBtNode *rn = (const struct CarafsBtNode *)(v.mem + rootcn->tree_root * BS);
    CHECK(rn->level >= 2, "tree at least three levels deep");

    // Logarithmic lookup: a batch of random-name lookups must cost only
    // a handful of block reads each — emphatically not O(N).
    constexpr u32 PROBES = 4000;
    u64 reads_before = m.stat_bdev_reads;
    u32 lcg = 0x12345678u;
    for (u32 k = 0; k < PROBES; k++) {
        lcg = lcg * 1664525u + 1013904223u;
        u32 i = lcg % N;
        snprintf(name, sizeof(name), "L%07u", i);
        u64 cn;
        CHECK(Carafs_DirLookup(&m, root, name, (u32)strlen(name), &cn, nullptr) == CARA_EOK &&
                  cn == anchor,
              "scale lookup hit");
    }
    u64 reads = m.stat_bdev_reads - reads_before;
    printf("  scale: %u lookups, %llu block reads (%.2f/lookup)\n", PROBES,
           (unsigned long long)reads, (double)reads / PROBES);
    CHECK(reads <= (u64)PROBES * 8, "lookup cost is logarithmic");

    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    vol_free(&v);
}

int main(void)
{
    test_basic();
    test_promotion();
    test_links();
    test_nested();
    test_scale();

    if (failures) {
        printf("test_carafs_dir: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_carafs_dir: ok\n");
    return 0;
}
