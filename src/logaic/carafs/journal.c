// SPDX-License-Identifier: BSD-2-Clause
//
// Metadata write-ahead log (F4; CARAFS.md §3.9).
//
// A circular physical-block WAL over the journal region. Each commit
// appends one transaction:
//
//     DESC | image[0] | image[1] | ... | image[n-1] | COMMIT
//
// DESC names the n home block numbers; the images are verbatim copies
// of the txn's metadata blocks; COMMIT carries a CRC chained over the
// images. A transaction is durable once COMMIT is on disk and flushed.
// Home writes stay lazy — committed images live in the cache (dirty)
// and reach home on eviction or at a checkpoint, which flushes every
// dirty block and then advances the JSB past the whole log.
//
// Recovery (replay): from the JSB's (head, seq), apply every complete
// transaction's images home in order — validated by per-block CRCs, the
// chained image CRC, and a contiguous sequence number — and discard the
// first torn one. Bounded by the journal size, never the volume size.
//
// Ordering (data=ordered) lives in cache.c's txn_commit: file data is
// written and flushed before carafs_journal_append runs, so a committed
// transaction's metadata never points at unwritten data.
//
// Log offsets are 1..journal_blocks-1 (offset 0 is the JSB); the
// physical block of offset o is journal_start + o. A transaction may
// wrap the ring; every block is addressed through j_adv, so wrapping is
// transparent to both append and replay.

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

static u64 j_phys(const struct CarafsMount *m, u32 off)
{
    return m->sb.journal_start + off;
}

// Advance a log offset by k blocks, wrapping within [1, j_log_blocks].
static u32 j_adv(const struct CarafsMount *m, u32 off, u32 k)
{
    u32 o = off + k;
    while (o > m->j_log_blocks) {
        o -= m->j_log_blocks;
    }
    return o;
}

// Blocks currently occupied by un-checkpointed transactions (0 = empty).
static u32 j_used(const struct CarafsMount *m)
{
    u32 d = m->j_tail >= m->j_head ? m->j_tail - m->j_head
                                   : m->j_tail + m->j_log_blocks - m->j_head;
    return d;
}

static bool block_crc_ok(const u8 *blk, u32 bs, u32 crc_off)
{
    u32 stored;
    memcpy(&stored, blk + crc_off, 4);
    return stored == Carafs_BlockCrc(blk, bs, crc_off);
}

// Persist the in-memory (head, seq) into the JSB and flush.
static int jsb_write(struct CarafsMount *m)
{
    u32 bs = m->block_size;
    memset(m->j_scratch, 0, bs);
    struct CarafsJsb *j = (struct CarafsJsb *)m->j_scratch;
    j->magic = CARAFS_MAGIC_JSB;
    j->seq = m->j_head_seq;
    j->head = m->j_head;
    carafs_put_crc(m->j_scratch, bs, offsetof(struct CarafsJsb, crc32c));
    int rc = m->bdev->write(m->bdev->ctx, m->sb.journal_start, 1, m->j_scratch);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->stat_bdev_writes++;
    return m->bdev->flush(m->bdev->ctx);
}

[[nodiscard]] int carafs_journal_checkpoint(struct CarafsMount *m)
{
    // Every committed image is in the cache (dirty); flush them all home
    // + barrier, then the whole log is redundant and can be dropped.
    int rc = carafs_cache_sync(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->j_head = m->j_tail;
    m->j_head_seq = m->j_next_seq;
    m->stat_checkpoints++;
    return jsb_write(m);
}

[[nodiscard]] int carafs_journal_maybe_checkpoint(struct CarafsMount *m)
{
    if (j_used(m) >= m->j_log_blocks / 2) {
        return carafs_journal_checkpoint(m);
    }
    return CARA_EOK;
}

[[nodiscard]] int carafs_journal_append(struct CarafsMount *m)
{
    u32 bs = m->block_size;
    u32 n = m->txn_n;
    u32 need = n + 2; // DESC + images + COMMIT

    if (need > m->j_log_blocks) {
        return CARA_EOVERFLOW; // larger than the whole log (geometry guards)
    }
    // Make room: a checkpoint empties the log entirely.
    if (j_used(m) + need > m->j_log_blocks) {
        int rc = carafs_journal_checkpoint(m);
        if (rc != CARA_EOK) {
            return rc;
        }
    }

    u64 seq = m->j_next_seq;
    u32 off = m->j_tail;

    // DESC block: the home block numbers.
    memset(m->j_scratch, 0, bs);
    struct CarafsJdesc *d = (struct CarafsJdesc *)m->j_scratch;
    d->magic = CARAFS_MAGIC_JDESC;
    d->seq = seq;
    d->n_blocks = n;
    u64 *targets = (u64 *)(m->j_scratch + CARAFS_JDESC_TARGETS_OFF);
    for (u32 i = 0; i < n; i++) {
        targets[i] = m->txn_blocks[i];
    }
    carafs_put_crc(m->j_scratch, bs, offsetof(struct CarafsJdesc, crc32c));
    int rc = m->bdev->write(m->bdev->ctx, j_phys(m, off), 1, m->j_scratch);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->stat_bdev_writes++;

    // Image blocks: verbatim cache images, CRC chained for COMMIT.
    u32 txn_crc = 0;
    for (u32 i = 0; i < n; i++) {
        u8 *data;
        rc = carafs_cache_get(m, m->txn_blocks[i], CARAFS_GET_READ, &data, nullptr);
        if (rc != CARA_EOK) {
            return rc;
        }
        rc = m->bdev->write(m->bdev->ctx, j_phys(m, j_adv(m, off, 1 + i)), 1, data);
        if (rc == CARA_EOK) {
            m->stat_bdev_writes++;
            txn_crc = Carafs_Crc32c(txn_crc, data, bs);
        }
        carafs_cache_put(m, m->txn_blocks[i]);
        if (rc != CARA_EOK) {
            return rc;
        }
    }

    // COMMIT block: the durability point.
    memset(m->j_scratch, 0, bs);
    struct CarafsJcommit *c = (struct CarafsJcommit *)m->j_scratch;
    c->magic = CARAFS_MAGIC_JCOMMIT;
    c->seq = seq;
    c->txn_crc = txn_crc;
    c->n_blocks = n;
    carafs_put_crc(m->j_scratch, bs, offsetof(struct CarafsJcommit, crc32c));
    rc = m->bdev->write(m->bdev->ctx, j_phys(m, j_adv(m, off, 1 + n)), 1, m->j_scratch);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->stat_bdev_writes++;
    rc = m->bdev->flush(m->bdev->ctx);
    if (rc != CARA_EOK) {
        return rc;
    }

    m->j_tail = j_adv(m, off, need);
    m->j_next_seq = seq + 1;
    return CARA_EOK;
}

// Read the JSB and validate it; leaves it in m->j_scratch.
static int jsb_read(struct CarafsMount *m, struct CarafsJsb *out)
{
    m->j_log_blocks = m->sb.journal_blocks - 1;
    int rc = m->bdev->read(m->bdev->ctx, m->sb.journal_start, 1, m->j_scratch);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->stat_bdev_reads++;
    const struct CarafsJsb *j = (const struct CarafsJsb *)m->j_scratch;
    if (j->magic != CARAFS_MAGIC_JSB ||
        !block_crc_ok(m->j_scratch, m->block_size, offsetof(struct CarafsJsb, crc32c))) {
        return CARA_EBADMAGIC;
    }
    if (j->head < 1 || j->head > m->j_log_blocks) {
        return CARA_EINVAL;
    }
    *out = *j;
    return CARA_EOK;
}

[[nodiscard]] int carafs_journal_init(struct CarafsMount *m)
{
    struct CarafsJsb j;
    int rc = jsb_read(m, &j);
    if (rc != CARA_EOK) {
        return rc;
    }
    // A clean volume has an empty log: head == tail.
    m->j_head = j.head;
    m->j_head_seq = j.seq;
    m->j_tail = j.head;
    m->j_next_seq = j.seq;
    return CARA_EOK;
}

[[nodiscard]] int carafs_journal_replay(struct CarafsMount *m)
{
    u32 bs = m->block_size;
    struct CarafsJsb j;
    int rc = jsb_read(m, &j);
    if (rc != CARA_EOK) {
        return rc;
    }
    u32 off = j.head;
    u64 seq = j.seq;
    u64 targets[CARAFS_TXN_MAX_BLOCKS];

    for (;;) {
        // DESC.
        rc = m->bdev->read(m->bdev->ctx, j_phys(m, off), 1, m->j_scratch);
        if (rc != CARA_EOK) {
            return rc;
        }
        m->stat_bdev_reads++;
        const struct CarafsJdesc *d = (const struct CarafsJdesc *)m->j_scratch;
        if (d->magic != CARAFS_MAGIC_JDESC ||
            !block_crc_ok(m->j_scratch, bs, offsetof(struct CarafsJdesc, crc32c)) ||
            d->seq != seq) {
            break; // end of the chain (or torn DESC)
        }
        u32 n = d->n_blocks;
        if (n == 0 || n > CARAFS_TXN_MAX_BLOCKS) {
            break;
        }
        bool ok = true;
        for (u32 i = 0; i < n; i++) {
            targets[i] = ((const u64 *)(m->j_scratch + CARAFS_JDESC_TARGETS_OFF))[i];
            if (targets[i] >= m->sb.total_blocks) {
                ok = false;
            }
        }
        if (!ok) {
            break;
        }

        // Pass 1: chain the image CRC (the home write must wait until
        // COMMIT validates — a torn final txn must not be applied).
        u32 txn_crc = 0;
        for (u32 i = 0; i < n; i++) {
            rc = m->bdev->read(m->bdev->ctx, j_phys(m, j_adv(m, off, 1 + i)), 1, m->j_image);
            if (rc != CARA_EOK) {
                return rc;
            }
            m->stat_bdev_reads++;
            txn_crc = Carafs_Crc32c(txn_crc, m->j_image, bs);
        }

        // COMMIT.
        rc = m->bdev->read(m->bdev->ctx, j_phys(m, j_adv(m, off, 1 + n)), 1, m->j_scratch);
        if (rc != CARA_EOK) {
            return rc;
        }
        m->stat_bdev_reads++;
        const struct CarafsJcommit *c = (const struct CarafsJcommit *)m->j_scratch;
        if (c->magic != CARAFS_MAGIC_JCOMMIT ||
            !block_crc_ok(m->j_scratch, bs, offsetof(struct CarafsJcommit, crc32c)) ||
            c->seq != seq || c->n_blocks != n || c->txn_crc != txn_crc) {
            break; // incomplete transaction — discard it and stop
        }

        // Pass 2: the transaction is whole — write its images home.
        for (u32 i = 0; i < n; i++) {
            rc = m->bdev->read(m->bdev->ctx, j_phys(m, j_adv(m, off, 1 + i)), 1, m->j_image);
            if (rc != CARA_EOK) {
                return rc;
            }
            m->stat_bdev_reads++;
            rc = m->bdev->write(m->bdev->ctx, targets[i], 1, m->j_image);
            if (rc != CARA_EOK) {
                return rc;
            }
            m->stat_bdev_writes++;
        }
        off = j_adv(m, off, n + 2);
        seq++;
    }

    rc = m->bdev->flush(m->bdev->ctx);
    if (rc != CARA_EOK) {
        return rc;
    }
    // The applied transactions are home; drop them from the log.
    m->j_head = off;
    m->j_head_seq = seq;
    m->j_tail = off;
    m->j_next_seq = seq;
    return jsb_write(m);
}
