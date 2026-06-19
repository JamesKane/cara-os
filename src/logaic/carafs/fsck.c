// SPDX-License-Identifier: BSD-2-Clause
//
// Carafs_Fsck (F1): read-only structural checker. Grows cross-checks
// epic by epic; the F1 version validates everything mkfs lays down —
// superblock geometry + crc, backup superblock, JSB, every AG header
// (self-address, crc, exact free count vs popcount, range bits), and
// the root cnode. The normal crash path is journal replay (§3.9);
// fsck is the development / disaster tool.

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

struct Fsck {
    struct CarafsBdev *bdev;
    u8 *blk;  // scratch block 0
    u8 *blk2; // scratch block 1
    struct CarafsFsckReport *rep;
};

static void fail(struct Fsck *f, const char *what, u64 block)
{
    f->rep->errors++;
    if (!f->rep->first_error) {
        f->rep->first_error = what;
        f->rep->first_error_block = block;
    }
}

static bool crc_ok(const u8 *blk, u32 bs, u32 crc_off)
{
    u32 stored;
    memcpy(&stored, blk + crc_off, 4);
    return stored == Carafs_BlockCrc(blk, bs, crc_off);
}

static u32 popcount_bits(const u8 *bits, u32 n_bytes)
{
    u32 n = 0;
    for (u32 i = 0; i < n_bytes; i++) {
        u8 v = bits[i];
        while (v) {
            n += v & 1u;
            v >>= 1;
        }
    }
    return n;
}

static bool bit_get(const u8 *bits, u32 idx)
{
    return (bits[idx / 8] >> (idx % 8)) & 1u;
}

// ---- Directory checks (F3) ----------------------------------------------------
//
// Walk one directory level (the root, for now): every dirent must
// resolve to an in-range cnode of the matching type, and subdirectories
// must point their parent_cnode back. Reads the dir's tree nodes one at
// a time into blk2 and each target cnode into blk — no extra scratch.
// A full recursive walk with link-count reconciliation is a later step.

constexpr u32 FSCK_BT_MAX_DEPTH = 8;

static void check_child(struct Fsck *f, const struct CarafsGeometry *g, u64 dir_block,
                        const struct CarafsDirent *de)
{
    u32 bs = f->bdev->block_size;
    if (de->name_len < 1) {
        fail(f, "dirent name length", dir_block);
    }
    if (de->type != CARAFS_T_FILE && de->type != CARAFS_T_DIR && de->type != CARAFS_T_SYMLINK) {
        fail(f, "dirent type", dir_block);
    }
    if (de->cnode < g->ag_first || de->cnode >= g->backup_sb) {
        fail(f, "dirent cnode out of range", de->cnode);
        return;
    }
    if (f->bdev->read(f->bdev->ctx, de->cnode, 1, f->blk) != CARA_EOK) {
        return;
    }
    f->rep->blocks_checked++;
    const struct CarafsCnode *c = (const struct CarafsCnode *)f->blk;
    if (c->magic != CARAFS_MAGIC_CNODE ||
        !crc_ok(f->blk, bs, offsetof(struct CarafsCnode, crc32c))) {
        fail(f, "dirent target cnode", de->cnode);
        return;
    }
    if (c->block_no != de->cnode) {
        fail(f, "dirent target self-address", de->cnode);
    }
    if (c->type != de->type) {
        fail(f, "dirent type vs cnode", de->cnode);
    }
    if (c->link_count < 1) {
        fail(f, "dirent target link_count", de->cnode);
    }
    if (c->type == CARAFS_T_DIR && c->parent_cnode != dir_block) {
        fail(f, "subdirectory parent linkage", de->cnode);
    }
}

// Validate a packed CarafsDirent blob (inline item payload or a leaf
// body). `blob` lives in blk2; child reads land in blk.
static void check_dirent_blob(struct Fsck *f, const struct CarafsGeometry *g, u64 dir_block,
                              const u8 *blob, u32 bytes)
{
    for (u32 off = 0; off + CARAFS_DIRENT_BASE <= bytes;) {
        const struct CarafsDirent *d = (const struct CarafsDirent *)(blob + off);
        u32 stride = Carafs_Align8(CARAFS_DIRENT_BASE + d->name_len);
        if (off + stride > bytes) {
            fail(f, "dirent overruns area", dir_block);
            break;
        }
        check_child(f, g, dir_block, d);
        off += stride;
    }
}

static void check_dir(struct Fsck *f, const struct CarafsGeometry *g, u64 dir_block)
{
    u32 bs = f->bdev->block_size;
    if (f->bdev->read(f->bdev->ctx, dir_block, 1, f->blk2) != CARA_EOK) {
        return;
    }
    const struct CarafsCnode *dcn = (const struct CarafsCnode *)f->blk2;
    u64 tree_root = dcn->tree_root;
    if (tree_root == 0) {
        u16 ilen = 0;
        u8 *p = carafs_item_find((struct CarafsCnode *)f->blk2, bs, CARAFS_ITEM_INLINE_DIRENTS,
                                 &ilen);
        if (p) {
            check_dirent_blob(f, g, dir_block, p, ilen);
        }
        return;
    }
    // DFS over the directory tree, one node in blk2 at a time. The stack
    // holds block numbers + a per-interior child cursor; each visit
    // re-reads the node (cheap for fsck, no second node buffer needed).
    struct {
        u64 block;
        u32 next;
    } st[FSCK_BT_MAX_DEPTH];
    u32 sp = 0;
    st[sp++] = (typeof(st[0])){ .block = tree_root };
    while (sp > 0) {
        u64 blk = st[sp - 1].block;
        if (blk < g->ag_first || blk >= g->backup_sb) {
            fail(f, "dir tree node out of range", blk);
            sp--;
            continue;
        }
        if (f->bdev->read(f->bdev->ctx, blk, 1, f->blk2) != CARA_EOK) {
            sp--;
            continue;
        }
        f->rep->blocks_checked++;
        const struct CarafsBtNode *n = (const struct CarafsBtNode *)f->blk2;
        if (n->magic != CARAFS_MAGIC_BTREE || n->block_no != blk || n->flavour != CARAFS_BT_DIR ||
            !crc_ok(f->blk2, bs, offsetof(struct CarafsBtNode, crc32c))) {
            fail(f, "dir tree node", blk);
            sp--;
            continue;
        }
        if (n->level == 0) {
            check_dirent_blob(f, g, dir_block, (const u8 *)f->blk2 + CARAFS_BT_BODY_OFF,
                              n->used_bytes);
            sp--;
            continue;
        }
        if (st[sp - 1].next < n->n_records) {
            const struct CarafsBtInteriorRec *r =
                (const struct CarafsBtInteriorRec *)((const u8 *)f->blk2 + CARAFS_BT_BODY_OFF);
            u64 child = r[st[sp - 1].next].child;
            st[sp - 1].next++;
            if (sp >= FSCK_BT_MAX_DEPTH) {
                fail(f, "dir tree too deep", blk);
                break;
            }
            st[sp++] = (typeof(st[0])){ .block = child };
        } else {
            sp--;
        }
    }
}

[[nodiscard]] int Carafs_Fsck(struct CarafsBdev *bdev, void *scratch, usize scratch_bytes,
                              struct CarafsFsckReport *rep)
{
    if (!bdev || !scratch || !rep || scratch_bytes < 2ull * bdev->block_size) {
        return CARA_EINVAL;
    }
    *rep = (struct CarafsFsckReport){ 0 };
    u32 bs = bdev->block_size;
    struct Fsck f = { .bdev = bdev, .blk = scratch, .blk2 = (u8 *)scratch + bs, .rep = rep };

    // ---- Superblock -------------------------------------------------------
    int rc = bdev->read(bdev->ctx, 0, 1, f.blk);
    if (rc != CARA_EOK) {
        return rc;
    }
    rep->blocks_checked++;
    const struct CarafsSuperblock *sb = (const struct CarafsSuperblock *)f.blk;
    if (memcmp(sb->magic, CARAFS_SB_MAGIC, 8) != 0) {
        fail(&f, "superblock magic", 0);
        return CARA_EOK; // nothing else is interpretable
    }
    if (!crc_ok(f.blk, bs, offsetof(struct CarafsSuperblock, crc32c))) {
        fail(&f, "superblock crc", 0);
        return CARA_EOK;
    }
    if (sb->version != CARAFS_FORMAT_VERSION) {
        fail(&f, "superblock version", 0);
        return CARA_EOK;
    }
    if ((1u << sb->block_size_log2) != bs) {
        fail(&f, "superblock block size vs bdev", 0);
        return CARA_EOK;
    }
    if (sb->total_blocks > bdev->n_blocks || sb->total_blocks < 16) {
        fail(&f, "superblock total_blocks vs bdev", 0);
        return CARA_EOK;
    }

    // Geometry must equal the canonical computation.
    struct CarafsGeometry g;
    if (carafs_geometry(&g, bs, sb->total_blocks) != CARA_EOK ||
        sb->journal_start != g.journal_start || sb->journal_blocks != g.journal_blocks ||
        sb->ag_first_block != g.ag_first || sb->ag_size_blocks != g.ag_size ||
        sb->ag_count != g.ag_count || sb->backup_sb != g.backup_sb) {
        fail(&f, "superblock geometry", 0);
        return CARA_EOK;
    }
    if (sb->root_cnode < g.ag_first || sb->root_cnode >= g.backup_sb) {
        fail(&f, "root cnode out of range", sb->root_cnode);
    }
    if (sb->name_len < 1 || sb->name_len > 63) {
        fail(&f, "volume name length", 0);
    }
    bool sb_dirty = sb->state == CARAFS_STATE_DIRTY;
    if (sb->state != CARAFS_STATE_CLEAN && sb->state != CARAFS_STATE_DIRTY) {
        fail(&f, "superblock state", 0);
    }
    if (sb_dirty) {
        rep->warnings++; // dirty is legal (crashed volume) but notable
    }
    u64 sb_free = sb->free_blocks;
    u64 root_cnode = sb->root_cnode;

    // ---- Backup superblock ------------------------------------------------
    rc = bdev->read(bdev->ctx, g.backup_sb, 1, f.blk2);
    if (rc != CARA_EOK) {
        return rc;
    }
    rep->blocks_checked++;
    const struct CarafsSuperblock *bsb = (const struct CarafsSuperblock *)f.blk2;
    if (memcmp(bsb->magic, CARAFS_SB_MAGIC, 8) != 0 ||
        !crc_ok(f.blk2, bs, offsetof(struct CarafsSuperblock, crc32c))) {
        fail(&f, "backup superblock", g.backup_sb);
    } else if (memcmp(bsb->uuid, sb->uuid, 16) != 0) {
        fail(&f, "backup superblock uuid mismatch", g.backup_sb);
    }

    // ---- JSB ---------------------------------------------------------------
    rc = bdev->read(bdev->ctx, g.journal_start, 1, f.blk2);
    if (rc != CARA_EOK) {
        return rc;
    }
    rep->blocks_checked++;
    const struct CarafsJsb *jsb = (const struct CarafsJsb *)f.blk2;
    if (jsb->magic != CARAFS_MAGIC_JSB || !crc_ok(f.blk2, bs, offsetof(struct CarafsJsb, crc32c))) {
        fail(&f, "journal superblock", g.journal_start);
    } else if (jsb->head < 1 || jsb->head >= g.journal_blocks) {
        fail(&f, "journal head out of range", g.journal_start);
    }

    // ---- AG bitmaps ---------------------------------------------------------
    u64 free_total = 0;
    for (u32 ag = 0; ag < g.ag_count; ag++) {
        u64 start = g.ag_first + (u64)ag * g.ag_size;
        u64 end = carafs_min_u64(start + g.ag_size, g.backup_sb);
        u32 covered = (u32)(end - start);

        rc = bdev->read(bdev->ctx, start, 1, f.blk2);
        if (rc != CARA_EOK) {
            return rc;
        }
        rep->blocks_checked++;
        const struct CarafsAgHeader *agh = (const struct CarafsAgHeader *)f.blk2;
        const u8 *bits = f.blk2 + CARAFS_AG_BITS_OFF;
        if (agh->magic != CARAFS_MAGIC_BITMAP) {
            fail(&f, "AG header magic", start);
            continue;
        }
        if (!crc_ok(f.blk2, bs, offsetof(struct CarafsAgHeader, crc32c))) {
            fail(&f, "AG header crc", start);
            continue;
        }
        if (agh->block_no != start) {
            fail(&f, "AG header self-address", start);
        }
        if (!bit_get(bits, 0)) {
            fail(&f, "AG bitmap block not marked allocated", start);
        }
        for (u32 b = covered; b < g.ag_size; b++) {
            if (!bit_get(bits, b)) {
                fail(&f, "AG out-of-range bit clear", start);
                break;
            }
        }
        u32 allocated = popcount_bits(bits, g.ag_size / 8);
        u32 free_count = g.ag_size - allocated;
        if (free_count != agh->free_count) {
            fail(&f, "AG free_count vs popcount", start);
        }
        free_total += agh->free_count;
    }
    // free_blocks in the superblock is advisory but should match on a
    // clean volume.
    if (!sb_dirty && free_total != sb_free) {
        fail(&f, "superblock free_blocks vs AG totals", 0);
    }

    // ---- Root cnode ---------------------------------------------------------
    rc = bdev->read(bdev->ctx, root_cnode, 1, f.blk2);
    if (rc != CARA_EOK) {
        return rc;
    }
    rep->blocks_checked++;
    const struct CarafsCnode *root = (const struct CarafsCnode *)f.blk2;
    bool root_ok = true;
    if (root->magic != CARAFS_MAGIC_CNODE ||
        !crc_ok(f.blk2, bs, offsetof(struct CarafsCnode, crc32c))) {
        fail(&f, "root cnode", root_cnode);
        root_ok = false;
    } else {
        if (root->block_no != root_cnode) {
            fail(&f, "root cnode self-address", root_cnode);
        }
        if (root->type != CARAFS_T_DIR) {
            fail(&f, "root cnode not a directory", root_cnode);
            root_ok = false;
        }
        if (root->link_count < 1) {
            fail(&f, "root cnode link_count", root_cnode);
        }
    }

    // ---- Root directory contents (F3) ---------------------------------------
    // (Reuses f.blk and f.blk2; the superblock fields needed below are
    // already captured in locals + g.)
    if (root_ok) {
        check_dir(&f, &g, root_cnode);
    }

    return CARA_EOK;
}
