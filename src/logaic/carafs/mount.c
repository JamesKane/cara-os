// SPDX-License-Identifier: BSD-2-Clause
//
// Carafs_Mount / Unmount / Sync (F2; CARAFS.md §4) plus the
// superblock write-back and the per-operation transaction bracket.
//
// The mounter supplies the whole cache arena (the core never
// allocates): Mount carves it into the entry table, the bucket
// index, and the block frames. The superblock state machine is
// CLEAN → DIRTY on the first mounted write (durably, before any
// other metadata of that write lands), DIRTY → CLEAN at unmount.
// Journal replay of a DIRTY volume arrives with F4.

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

static u8 *align_up(u8 *p, usize a)
{
    return (u8 *)(((uintptr_t)p + (a - 1)) & ~(uintptr_t)(a - 1));
}

// Carve `n` cache entries out of the arena; returns false when it
// doesn't fit. Layout: ents | buckets (pow2 >= n) | block frames.
static bool carve(struct CarafsMount *m, void *mem, usize bytes, u32 n)
{
    u32 n_buckets = 1;
    while (n_buckets < n) {
        n_buckets <<= 1;
    }
    u8 *base = mem;
    u8 *end = base + bytes;
    u8 *p = align_up(base, alignof(struct CarafsCacheEnt));
    struct CarafsCacheEnt *ents = (struct CarafsCacheEnt *)p;
    p = align_up(p + (usize)n * sizeof(struct CarafsCacheEnt), alignof(u32));
    u32 *buckets = (u32 *)p;
    // Block frames at 64: enough for any in-core access; the kernel
    // NVMe binding's PRP alignment is satisfied by the arena it
    // chooses to pass (F5).
    p = align_up(p + (usize)n_buckets * sizeof(u32), 64);
    u8 *frames = p;
    // n cache frames + two journal scratch blocks (DESC/COMMIT build,
    // replay image reads).
    if (p + (usize)(n + 2) * m->block_size > end) {
        return false;
    }
    m->n_ents = n;
    m->ents = ents;
    m->n_buckets = n_buckets;
    m->buckets = buckets;
    m->block_mem = frames;
    m->j_scratch = frames + (usize)n * m->block_size;
    m->j_image = frames + (usize)(n + 1) * m->block_size;
    return true;
}

static void cache_init(struct CarafsMount *m)
{
    for (u32 b = 0; b < m->n_buckets; b++) {
        m->buckets[b] = CARAFS_ENT_NONE;
    }
    // Chain every (empty) entry onto the LRU list so ent_obtain finds
    // them from the cold end.
    for (u32 i = 0; i < m->n_ents; i++) {
        m->ents[i] = (struct CarafsCacheEnt){
            .block = CARAFS_BLOCK_NONE,
            .lru_prev = (i == 0) ? CARAFS_ENT_NONE : i - 1,
            .lru_next = (i == m->n_ents - 1) ? CARAFS_ENT_NONE : i + 1,
            .hash_next = CARAFS_ENT_NONE,
            .data = m->block_mem + (usize)i * m->block_size,
        };
    }
    m->lru_head = 0;
    m->lru_tail = m->n_ents - 1;
}

// Serialize m->sb into the (cached, pinned) block-0 image.
static void sb_serialize(struct CarafsMount *m, u8 *blk)
{
    memcpy(blk, &m->sb, sizeof(m->sb));
    carafs_put_crc(blk, m->block_size, offsetof(struct CarafsSuperblock, crc32c));
}

// Write m->sb straight home + flush — the durability points outside
// the txn path (mark-dirty, unmount-clean).
static int sb_write_direct(struct CarafsMount *m)
{
    u8 *blk;
    int rc = carafs_cache_get(m, 0, CARAFS_GET_READ, &blk, nullptr);
    if (rc != CARA_EOK) {
        return rc;
    }
    sb_serialize(m, blk);
    rc = m->bdev->write(m->bdev->ctx, 0, 1, blk);
    carafs_cache_put(m, 0);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->stat_bdev_writes++;
    return m->bdev->flush(m->bdev->ctx);
}

[[nodiscard]] int Carafs_Mount(struct CarafsMount *m, struct CarafsBdev *bdev,
                               const struct CarafsMountOpts *opts)
{
    if (!m || !bdev || !opts || !opts->cache_mem) {
        return CARA_EINVAL;
    }
    *m = (struct CarafsMount){ 0 };
    m->bdev = bdev;
    m->block_size = bdev->block_size;
    m->readonly = opts->readonly;
    m->now_ns = opts->now_ns;
    m->now_ctx = opts->now_ctx;

    // Size the cache: per entry one block frame + the entry + its
    // share of the bucket array (pow2 rounding bounds it by 8 B).
    usize per = m->block_size + sizeof(struct CarafsCacheEnt) + 8;
    u32 n = (u32)carafs_min_u64(opts->cache_bytes / per, 0xFFFFFFF0u);
    while (n >= CARAFS_CACHE_MIN_BLOCKS && !carve(m, opts->cache_mem, opts->cache_bytes, n)) {
        n--;
    }
    if (n < CARAFS_CACHE_MIN_BLOCKS) {
        return CARA_ENOMEM;
    }
    cache_init(m);

    // Superblock: magic, crc, version, features, geometry vs bdev.
    u8 *blk;
    int rc = carafs_cache_get(m, 0, CARAFS_GET_READ, &blk, nullptr);
    if (rc != CARA_EOK) {
        return rc;
    }
    const struct CarafsSuperblock *sb = (const struct CarafsSuperblock *)blk;
    rc = CARA_EBADMAGIC;
    u32 stored_crc;
    memcpy(&stored_crc, blk + offsetof(struct CarafsSuperblock, crc32c), 4);
    if (memcmp(sb->magic, CARAFS_SB_MAGIC, 8) != 0 ||
        stored_crc !=
            Carafs_BlockCrc(blk, m->block_size, offsetof(struct CarafsSuperblock, crc32c))) {
        goto out;
    }
    rc = CARA_EBADVERSION;
    if (sb->version != CARAFS_FORMAT_VERSION || sb->incompat != 0) {
        goto out;
    }
    rc = CARA_EINVAL;
    struct CarafsGeometry g;
    if ((1ull << sb->block_size_log2) != m->block_size || sb->total_blocks > bdev->n_blocks ||
        carafs_geometry(&g, m->block_size, sb->total_blocks) != CARA_EOK ||
        sb->journal_start != g.journal_start || sb->journal_blocks != g.journal_blocks ||
        sb->ag_first_block != g.ag_first || sb->ag_size_blocks != g.ag_size ||
        sb->ag_count != g.ag_count || sb->backup_sb != g.backup_sb || sb->root_cnode < g.ag_first ||
        sb->root_cnode >= g.backup_sb) {
        goto out;
    }
    if (sb->ro_compat != 0) {
        m->readonly = true; // unknown ro_compat feature: read, don't write
    }
    memcpy(&m->sb, sb, sizeof(m->sb));
    carafs_cache_put(m, 0);
    carafs_cache_invalidate(m, 0); // replay/init reread block 0 fresh
    m->j_log_blocks = m->sb.journal_blocks - 1;

    // A DIRTY state is an unclean detach: replay the journal (§3.9).
    // Replay needs to write, so a read-only mount of a dirty volume
    // skips it and yields the (possibly stale) pre-crash home image —
    // the documented dev/disaster path; the normal route is RW replay.
    if (m->sb.state == CARAFS_STATE_DIRTY && !m->readonly) {
        rc = carafs_journal_replay(m);
        if (rc != CARA_EOK) {
            return rc;
        }
        // Reload the recovered superblock, then mark the volume clean.
        u8 *b2;
        rc = carafs_cache_get(m, 0, CARAFS_GET_READ, &b2, nullptr);
        if (rc != CARA_EOK) {
            return rc;
        }
        memcpy(&m->sb, b2, sizeof(m->sb));
        carafs_cache_put(m, 0);
        m->sb.state = CARAFS_STATE_CLEAN;
        rc = sb_write_direct(m);
        if (rc != CARA_EOK) {
            return rc;
        }
    } else {
        rc = carafs_journal_init(m);
        if (rc != CARA_EOK) {
            return rc;
        }
    }
    m->mounted = true;
    return CARA_EOK;
out:
    carafs_cache_put(m, 0);
    return rc;
}

[[nodiscard]] int Carafs_Sync(struct CarafsMount *m)
{
    if (!m || !m->mounted) {
        return CARA_EINVAL;
    }
    if (m->readonly) {
        return carafs_cache_sync(m);
    }
    // Checkpoint: home-write every committed image and empty the log.
    return carafs_journal_checkpoint(m);
}

[[nodiscard]] int Carafs_Unmount(struct CarafsMount *m)
{
    if (!m || !m->mounted) {
        return CARA_EINVAL;
    }
    if (m->readonly) {
        int rc = carafs_cache_sync(m);
        if (rc != CARA_EOK) {
            return rc;
        }
        m->mounted = false;
        return CARA_EOK;
    }
    // Flush every committed image home and empty the log, then drop the
    // DIRTY flag — a clean volume needs no replay on the next mount.
    int rc = carafs_journal_checkpoint(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (m->sb.state == CARAFS_STATE_DIRTY) {
        m->sb.state = CARAFS_STATE_CLEAN;
        u64 now = carafs_now(m);
        if (now) {
            m->sb.modified_ns = now;
        }
        rc = sb_write_direct(m);
        if (rc != CARA_EOK) {
            return rc;
        }
    }
    m->mounted = false;
    return CARA_EOK;
}

// ---- Txn-path superblock + the operation bracket ------------------------------

[[nodiscard]] int carafs_sb_write(struct CarafsMount *m)
{
    u8 *blk;
    int rc = carafs_cache_get(m, 0, CARAFS_GET_READ, &blk, nullptr);
    if (rc != CARA_EOK) {
        return rc;
    }
    sb_serialize(m, blk);
    rc = carafs_txn_dirty(m, 0);
    carafs_cache_put(m, 0);
    return rc;
}

[[nodiscard]] int carafs_mark_dirty(struct CarafsMount *m)
{
    if (m->sb.state == CARAFS_STATE_DIRTY) {
        return CARA_EOK;
    }
    m->sb.state = CARAFS_STATE_DIRTY;
    int rc = sb_write_direct(m);
    if (rc != CARA_EOK) {
        m->sb.state = CARAFS_STATE_CLEAN;
    }
    return rc;
}

[[nodiscard]] int carafs_op_begin(struct CarafsMount *m)
{
    if (!m || !m->mounted || m->readonly) {
        return CARA_EINVAL;
    }
    int rc = carafs_mark_dirty(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->sb_at_txn = m->sb;
    carafs_txn_begin(m);
    return CARA_EOK;
}

[[nodiscard]] int carafs_op_commit(struct CarafsMount *m)
{
    int rc = carafs_sb_write(m);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    rc = carafs_txn_commit(m);
    if (rc != CARA_EOK) {
        // The WAL append failed before the transaction became durable;
        // roll it back wholesale (drops the txn images, restores the sb).
        carafs_op_abort(m);
        return rc;
    }
    return CARA_EOK;
}

void carafs_op_abort(struct CarafsMount *m)
{
    carafs_txn_abort(m);
    m->sb = m->sb_at_txn;
}
