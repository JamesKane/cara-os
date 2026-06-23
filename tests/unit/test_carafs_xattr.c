// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS L11: the inline extended-attribute layer on a memory bdev —
// set/get/replace/remove of named attributes packed into the cnode's
// CARAFS_ITEM_XATTR item, multiple attributes coexisting, case-sensitive
// names, the not-present and overflow paths, and an fsck cross-check that
// the cnode stays structurally valid after the mutations.

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
    return rep.errors;
}

static void test_xattr(void)
{
    struct Vol v;
    vol_make(&v, 16ull * 1024 * 1024, 1u << 20);
    struct CarafsMount m;
    mount_vol(&m, &v);
    u64 root = m.sb.root_cnode;

    u64 f;
    CHECK(Carafs_DirCreate(&m, root, "icon", 4, CARAFS_T_FILE, &f) == CARA_EOK, "create file");

    u8 buf[256];
    usize len = 0;

    // Absent attribute → ENOENT.
    CHECK(Carafs_XattrGet(&m, f, "cara.icon", 9, buf, sizeof buf, &len) == CARA_ENOENT,
          "get absent");

    // Set + get round-trip.
    const char *v1 = "hello-icon-blob";
    CHECK(Carafs_XattrSet(&m, f, "cara.icon", 9, v1, 15) == CARA_EOK, "set cara.icon");
    len = 0;
    CHECK(Carafs_XattrGet(&m, f, "cara.icon", 9, buf, sizeof buf, &len) == CARA_EOK,
          "get cara.icon");
    CHECK(len == 15, "len matches");
    CHECK(memcmp(buf, v1, 15) == 0, "value matches");

    // A second attribute coexists; case-sensitive names are distinct.
    const char *note = "a longer free-form user note value";
    CHECK(Carafs_XattrSet(&m, f, "user.note", 9, note, (u32)strlen(note)) == CARA_EOK, "set note");
    CHECK(Carafs_XattrGet(&m, f, "CARA.ICON", 9, buf, sizeof buf, &len) == CARA_ENOENT,
          "names are case-sensitive");
    len = 0;
    CHECK(Carafs_XattrGet(&m, f, "user.note", 9, buf, sizeof buf, &len) == CARA_EOK, "get note");
    CHECK(len == strlen(note) && memcmp(buf, note, len) == 0, "note value");
    // cara.icon still intact after the second insert.
    len = 0;
    CHECK(Carafs_XattrGet(&m, f, "cara.icon", 9, buf, sizeof buf, &len) == CARA_EOK,
          "icon survives");
    CHECK(len == 15 && memcmp(buf, v1, 15) == 0, "icon value survives");

    // Replace cara.icon with a different-sized value (grows the record).
    const char *v2 = "a-much-longer-replacement-icon-blob-value";
    CHECK(Carafs_XattrSet(&m, f, "cara.icon", 9, v2, (u32)strlen(v2)) == CARA_EOK, "replace icon");
    len = 0;
    CHECK(Carafs_XattrGet(&m, f, "cara.icon", 9, buf, sizeof buf, &len) == CARA_EOK,
          "get replaced");
    CHECK(len == strlen(v2) && memcmp(buf, v2, len) == 0, "replaced value");
    // note untouched by the replace.
    len = 0;
    CHECK(Carafs_XattrGet(&m, f, "user.note", 9, buf, sizeof buf, &len) == CARA_EOK, "note after");
    CHECK(len == strlen(note) && memcmp(buf, note, len) == 0, "note value after");

    // Short buffer: rc OK, *len_out is the full length, bytes truncated.
    u8 small[4];
    len = 0;
    CHECK(Carafs_XattrGet(&m, f, "cara.icon", 9, small, sizeof small, &len) == CARA_EOK,
          "get into small buf");
    CHECK(len == strlen(v2), "len_out is full length");
    CHECK(memcmp(small, v2, 4) == 0, "first bytes copied");

    // Remove cara.icon; note remains; second remove → ENOENT.
    CHECK(Carafs_XattrRemove(&m, f, "cara.icon", 9) == CARA_EOK, "remove icon");
    CHECK(Carafs_XattrGet(&m, f, "cara.icon", 9, buf, sizeof buf, &len) == CARA_ENOENT,
          "icon gone");
    CHECK(Carafs_XattrRemove(&m, f, "cara.icon", 9) == CARA_ENOENT, "remove absent");
    len = 0;
    CHECK(Carafs_XattrGet(&m, f, "user.note", 9, buf, sizeof buf, &len) == CARA_EOK,
          "note survives remove");

    // Remove the last attribute → the whole xattr item is dropped.
    CHECK(Carafs_XattrRemove(&m, f, "user.note", 9) == CARA_EOK, "remove note");
    CHECK(Carafs_XattrGet(&m, f, "user.note", 9, buf, sizeof buf, &len) == CARA_ENOENT,
          "note gone");

    // Inline-only: a value too big for the cnode item area → EOVERFLOW.
    static u8 huge[BS];
    memset(huge, 0xAB, sizeof huge);
    CHECK(Carafs_XattrSet(&m, f, "cara.big", 8, huge, sizeof huge) == CARA_EOVERFLOW,
          "oversize → overflow");
    // The failed set left nothing behind.
    CHECK(Carafs_XattrGet(&m, f, "cara.big", 8, buf, sizeof buf, &len) == CARA_ENOENT,
          "no partial after overflow");

    // Persist across a remount: set, sync, unmount, remount, get.
    CHECK(Carafs_XattrSet(&m, f, "cara.icon", 9, v1, 15) == CARA_EOK, "set before remount");
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "unmount");
    CHECK(fsck_errors(&v) == 0, "fsck clean after xattr ops");
    mount_vol(&m, &v);
    len = 0;
    CHECK(Carafs_XattrGet(&m, f, "cara.icon", 9, buf, sizeof buf, &len) == CARA_EOK,
          "get after remount");
    CHECK(len == 15 && memcmp(buf, v1, 15) == 0, "value after remount");
    CHECK(Carafs_Unmount(&m) == CARA_EOK, "final unmount");

    vol_free(&v);
}

int main(void)
{
    test_xattr();

    if (failures) {
        printf("test_carafs_xattr: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_carafs_xattr: ok\n");
    return 0;
}
