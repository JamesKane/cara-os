// SPDX-License-Identifier: BSD-2-Clause
//
// Carafs_Mkfs (F1): lay an empty CaraFS volume onto a bdev per
// CARAFS.md §3.2 — superblock, idle journal, AG bitmap chain, empty
// root directory cnode, backup superblock. Shared by the host
// mkfs.carafs tool, the unit tests, and (F5) the kernel's
// format-on-first-boot path for scratch volumes.

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

static void fill_superblock(struct CarafsSuperblock *sb, const struct CarafsGeometry *g,
                            const struct CarafsMkfsOpts *opts, u64 free_blocks)
{
    *sb = (struct CarafsSuperblock){ 0 };
    memcpy(sb->magic, CARAFS_SB_MAGIC, 8);
    sb->version = CARAFS_FORMAT_VERSION;
    sb->block_size_log2 = 0;
    for (u32 b = g->block_size; b > 1; b >>= 1) {
        sb->block_size_log2++;
    }
    sb->state = CARAFS_STATE_CLEAN;
    sb->ag_size_blocks = g->ag_size;
    sb->total_blocks = g->total_blocks;
    sb->journal_start = g->journal_start;
    sb->journal_blocks = g->journal_blocks;
    sb->ag_count = g->ag_count;
    sb->ag_first_block = g->ag_first;
    sb->root_cnode = g->root_cnode;
    sb->free_blocks = free_blocks;
    sb->backup_sb = g->backup_sb;

    if (opts->uuid) {
        memcpy(sb->uuid, opts->uuid, 16);
    } else {
        // Best-effort derived UUID (no randomness in the core): hash
        // the name and the timestamp. Tools pass real random bytes.
        u64 h1 = Carafs_NameHash(opts->name, opts->name_len);
        u64 h2 = (h1 ^ opts->now_ns) * 1099511628211ull;
        memcpy(sb->uuid, &h1, 8);
        memcpy(sb->uuid + 8, &h2, 8);
    }
    sb->name_len = (u8)opts->name_len;
    memcpy(sb->name, opts->name, opts->name_len);
    sb->created_ns = opts->now_ns;
    sb->modified_ns = opts->now_ns;
}

[[nodiscard]] int Carafs_Mkfs(struct CarafsBdev *bdev, const struct CarafsMkfsOpts *opts,
                              void *scratch, usize scratch_bytes)
{
    if (!bdev || !opts || !scratch) {
        return CARA_EINVAL;
    }
    u32 bs = bdev->block_size;
    u32 log2 = opts->block_size_log2 ? opts->block_size_log2 : CARAFS_DEF_BLOCK_LOG2;
    if (log2 < CARAFS_MIN_BLOCK_LOG2 || log2 > CARAFS_MAX_BLOCK_LOG2 || (1u << log2) != bs) {
        return CARA_EINVAL;
    }
    if (scratch_bytes < bs) {
        return CARA_EINVAL;
    }
    if (!opts->name || opts->name_len < 1 || opts->name_len > 63 ||
        !Carafs_NameValid(opts->name, opts->name_len)) {
        return CARA_EINVAL;
    }

    struct CarafsGeometry g;
    int rc = carafs_geometry(&g, bs, bdev->n_blocks);
    if (rc != CARA_EOK) {
        return rc;
    }

    u8 *blk = scratch;

    // ---- Journal: JSB (seq 1, head 1 = empty log). The log blocks
    // themselves don't need initialising — replay is bounded by the
    // JSB head/seq and transaction CRCs.
    memset(blk, 0, bs);
    struct CarafsJsb *jsb = (struct CarafsJsb *)blk;
    jsb->magic = CARAFS_MAGIC_JSB;
    jsb->seq = 1;
    jsb->head = 1;
    carafs_put_crc(blk, bs, offsetof(struct CarafsJsb, crc32c));
    rc = bdev->write(bdev->ctx, g.journal_start, 1, blk);
    if (rc != CARA_EOK) {
        return rc;
    }

    // ---- AG bitmap chain. Bit set = allocated: bit 0 (the bitmap
    // block itself), the root cnode in AG 0, and every bit past the
    // AG's real coverage (partial last AG / backup-sb exclusion).
    u64 free_total = 0;
    for (u32 ag = 0; ag < g.ag_count; ag++) {
        u64 start = g.ag_first + (u64)ag * g.ag_size;
        u64 end = carafs_min_u64(start + g.ag_size, g.backup_sb);
        u32 covered = (u32)(end - start);

        memset(blk, 0, bs);
        struct CarafsAgHeader *agh = (struct CarafsAgHeader *)blk;
        agh->magic = CARAFS_MAGIC_BITMAP;
        agh->block_no = start;
        u8 *bits = blk + CARAFS_AG_BITS_OFF;

        bits[0] |= 0x01; // the bitmap block
        u32 used = 1;
        if (g.root_cnode >= start && g.root_cnode < end) {
            u32 rel = (u32)(g.root_cnode - start);
            bits[rel / 8] |= (u8)(1u << (rel % 8));
            used++;
        }
        for (u32 b = covered; b < g.ag_size; b++) {
            bits[b / 8] |= (u8)(1u << (b % 8));
        }
        agh->free_count = covered - used;
        agh->rotor = 0;
        free_total += agh->free_count;
        carafs_put_crc(blk, bs, offsetof(struct CarafsAgHeader, crc32c));
        rc = bdev->write(bdev->ctx, start, 1, blk);
        if (rc != CARA_EOK) {
            return rc;
        }
    }

    // ---- Root directory cnode: empty, no items.
    memset(blk, 0, bs);
    struct CarafsCnode *root = (struct CarafsCnode *)blk;
    root->magic = CARAFS_MAGIC_CNODE;
    root->block_no = g.root_cnode;
    root->generation = 1;
    root->type = CARAFS_T_DIR;
    root->link_count = 1;
    root->created_ns = opts->now_ns;
    root->modified_ns = opts->now_ns;
    root->changed_ns = opts->now_ns;
    carafs_put_crc(blk, bs, offsetof(struct CarafsCnode, crc32c));
    rc = bdev->write(bdev->ctx, g.root_cnode, 1, blk);
    if (rc != CARA_EOK) {
        return rc;
    }

    // ---- Superblock + backup (identical images).
    memset(blk, 0, bs);
    struct CarafsSuperblock *sb = (struct CarafsSuperblock *)blk;
    fill_superblock(sb, &g, opts, free_total);
    carafs_put_crc(blk, bs, offsetof(struct CarafsSuperblock, crc32c));
    rc = bdev->write(bdev->ctx, g.backup_sb, 1, blk);
    if (rc != CARA_EOK) {
        return rc;
    }
    rc = bdev->write(bdev->ctx, 0, 1, blk);
    if (rc != CARA_EOK) {
        return rc;
    }

    return bdev->flush(bdev->ctx);
}
