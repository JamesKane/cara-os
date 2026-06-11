// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS block cache + transaction bracket (F1; CARAFS.md §4).
//
// Fixed-capacity write-back cache carved from the caller's mount
// arena: entry table, bucket-chain index, LRU list, and the block
// data itself. Single-threaded by contract (one mounter; the Phase 3
// handler Gleas serialises by construction).
//
// Transactions: every mutating filesystem operation brackets its
// metadata writes in txn_begin / txn_dirty / txn_commit. Pre-journal
// (F1–F3) a commit writes the txn's blocks straight home and flushes;
// F4 redirects commit through the WAL and makes home writes lazy.
// TXN-flagged entries are pinned in effect: never evicted, so an
// uncommitted transaction can always abort by dropping them.

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

// ---- LRU helpers ------------------------------------------------------------

static void lru_unlink(struct CarafsMount *m, u32 idx)
{
    struct CarafsCacheEnt *e = &m->ents[idx];
    if (e->lru_prev != CARAFS_ENT_NONE) {
        m->ents[e->lru_prev].lru_next = e->lru_next;
    } else {
        m->lru_head = e->lru_next;
    }
    if (e->lru_next != CARAFS_ENT_NONE) {
        m->ents[e->lru_next].lru_prev = e->lru_prev;
    } else {
        m->lru_tail = e->lru_prev;
    }
    e->lru_prev = e->lru_next = CARAFS_ENT_NONE;
}

static void lru_push_mru(struct CarafsMount *m, u32 idx)
{
    struct CarafsCacheEnt *e = &m->ents[idx];
    e->lru_prev = CARAFS_ENT_NONE;
    e->lru_next = m->lru_head;
    if (m->lru_head != CARAFS_ENT_NONE) {
        m->ents[m->lru_head].lru_prev = idx;
    }
    m->lru_head = idx;
    if (m->lru_tail == CARAFS_ENT_NONE) {
        m->lru_tail = idx;
    }
}

// ---- Bucket index -----------------------------------------------------------

static u32 bucket_of(const struct CarafsMount *m, u64 block)
{
    // Multiplicative hash; n_buckets is a power of two.
    return (u32)((block * 0x9E3779B97F4A7C15ull) >> 32) & (m->n_buckets - 1);
}

static u32 index_find(struct CarafsMount *m, u64 block)
{
    u32 idx = m->buckets[bucket_of(m, block)];
    while (idx != CARAFS_ENT_NONE && m->ents[idx].block != block) {
        idx = m->ents[idx].hash_next;
    }
    return idx;
}

static void index_insert(struct CarafsMount *m, u32 idx)
{
    u32 b = bucket_of(m, m->ents[idx].block);
    m->ents[idx].hash_next = m->buckets[b];
    m->buckets[b] = idx;
}

static void index_remove(struct CarafsMount *m, u32 idx)
{
    u32 b = bucket_of(m, m->ents[idx].block);
    u32 *link = &m->buckets[b];
    while (*link != idx) {
        link = &m->ents[*link].hash_next;
    }
    *link = m->ents[idx].hash_next;
    m->ents[idx].hash_next = CARAFS_ENT_NONE;
}

// ---- Entry lifecycle --------------------------------------------------------

static int ent_writeback(struct CarafsMount *m, u32 idx)
{
    struct CarafsCacheEnt *e = &m->ents[idx];
    int rc = m->bdev->write(m->bdev->ctx, e->block, 1, e->data);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->stat_bdev_writes++;
    e->flags &= ~CARAFS_ENT_DIRTY;
    return CARA_EOK;
}

// Find a slot for a new block: an empty entry, else the coldest
// evictable one (unpinned, not TXN). Dirty victims are written back.
static int ent_obtain(struct CarafsMount *m, u32 *idx_out)
{
    // Empty slots first — block == CARAFS_BLOCK_NONE entries are kept
    // on the LRU list from init, so scan from the cold end.
    u32 idx = m->lru_tail;
    while (idx != CARAFS_ENT_NONE) {
        struct CarafsCacheEnt *e = &m->ents[idx];
        if (e->block == CARAFS_BLOCK_NONE) {
            *idx_out = idx;
            return CARA_EOK;
        }
        if (e->pins == 0 && !(e->flags & CARAFS_ENT_TXN)) {
            if (e->flags & CARAFS_ENT_DIRTY) {
                int rc = ent_writeback(m, idx);
                if (rc != CARA_EOK) {
                    return rc;
                }
            }
            index_remove(m, idx);
            e->block = CARAFS_BLOCK_NONE;
            *idx_out = idx;
            return CARA_EOK;
        }
        idx = m->ents[idx].lru_prev;
    }
    // Every entry pinned or in the txn — the mount arena is too small
    // for the operation in flight. CARAFS_TXN_MAX_BLOCKS plus pins is
    // sized well under CARAFS_CACHE_MIN_BLOCKS… this is a bug, not a
    // runtime condition.
    return CARA_ENOMEM;
}

[[nodiscard]] int carafs_cache_get(struct CarafsMount *m, u64 block, u32 mode, u8 **data_out)
{
    if (block >= m->bdev->n_blocks) {
        return CARA_ERANGE;
    }
    u32 idx = index_find(m, block);
    if (idx != CARAFS_ENT_NONE) {
        m->stat_cache_hits++;
    } else {
        int rc = ent_obtain(m, &idx);
        if (rc != CARA_EOK) {
            return rc;
        }
        struct CarafsCacheEnt *e = &m->ents[idx];
        if (mode == CARAFS_GET_ZERO) {
            memset(e->data, 0, m->block_size);
        } else {
            rc = m->bdev->read(m->bdev->ctx, block, 1, e->data);
            if (rc != CARA_EOK) {
                return rc;
            }
            m->stat_bdev_reads++;
        }
        e->block = block;
        e->flags = (mode == CARAFS_GET_ZERO) ? CARAFS_ENT_DIRTY : 0;
        index_insert(m, idx);
    }
    struct CarafsCacheEnt *e = &m->ents[idx];
    e->pins++;
    lru_unlink(m, idx);
    lru_push_mru(m, idx);
    *data_out = e->data;
    return CARA_EOK;
}

void carafs_cache_put(struct CarafsMount *m, u64 block)
{
    u32 idx = index_find(m, block);
    if (idx != CARAFS_ENT_NONE && m->ents[idx].pins > 0) {
        m->ents[idx].pins--;
    }
}

[[nodiscard]] int carafs_cache_sync(struct CarafsMount *m)
{
    for (u32 i = 0; i < m->n_ents; i++) {
        struct CarafsCacheEnt *e = &m->ents[i];
        if (e->block != CARAFS_BLOCK_NONE && (e->flags & CARAFS_ENT_DIRTY)) {
            int rc = ent_writeback(m, i);
            if (rc != CARA_EOK) {
                return rc;
            }
        }
    }
    return m->bdev->flush(m->bdev->ctx);
}

// ---- Transactions -----------------------------------------------------------

void carafs_txn_begin(struct CarafsMount *m)
{
    m->in_txn = true;
    m->txn_n = 0;
}

[[nodiscard]] int carafs_txn_dirty(struct CarafsMount *m, u64 block)
{
    u32 idx = index_find(m, block);
    if (idx == CARAFS_ENT_NONE) {
        return CARA_EINVAL; // caller must hold a cache_get pin
    }
    struct CarafsCacheEnt *e = &m->ents[idx];
    e->flags |= CARAFS_ENT_DIRTY;
    if (!m->in_txn) {
        return CARA_EOK; // mount-time / pre-txn writes sync via cache_sync
    }
    if (e->flags & CARAFS_ENT_TXN) {
        return CARA_EOK;
    }
    if (m->txn_n >= CARAFS_TXN_MAX_BLOCKS) {
        return CARA_EOVERFLOW;
    }
    e->flags |= CARAFS_ENT_TXN;
    m->txn_blocks[m->txn_n++] = block;
    return CARA_EOK;
}

[[nodiscard]] int carafs_txn_commit(struct CarafsMount *m)
{
    // Pre-journal commit (F1–F3): write the txn's blocks home, then a
    // durability barrier. F4 replaces the body of this function with
    // a WAL append + lazy home writes.
    int rc = CARA_EOK;
    for (u32 i = 0; i < m->txn_n; i++) {
        u32 idx = index_find(m, m->txn_blocks[i]);
        if (idx == CARAFS_ENT_NONE) {
            rc = CARA_EINVAL;
            break;
        }
        m->ents[idx].flags &= ~CARAFS_ENT_TXN;
        if (m->ents[idx].flags & CARAFS_ENT_DIRTY) {
            rc = ent_writeback(m, idx);
            if (rc != CARA_EOK) {
                break;
            }
        }
    }
    m->in_txn = false;
    m->txn_n = 0;
    if (rc != CARA_EOK) {
        return rc;
    }
    return m->bdev->flush(m->bdev->ctx);
}

void carafs_txn_abort(struct CarafsMount *m)
{
    // Drop the txn blocks from cache entirely — their cached state is
    // ahead of disk and must not leak out via later reads.
    for (u32 i = 0; i < m->txn_n; i++) {
        u32 idx = index_find(m, m->txn_blocks[i]);
        if (idx != CARAFS_ENT_NONE) {
            struct CarafsCacheEnt *e = &m->ents[idx];
            e->flags = 0;
            e->pins = 0;
            index_remove(m, idx);
            e->block = CARAFS_BLOCK_NONE;
        }
    }
    m->in_txn = false;
    m->txn_n = 0;
}
