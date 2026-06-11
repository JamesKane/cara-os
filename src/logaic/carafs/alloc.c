// SPDX-License-Identifier: BSD-2-Clause
//
// AG bitmap allocator (F2; CARAFS.md §3.8). Policy is advisory, not
// format: take the first free run at or after the AG's rotor cursor
// (up to the requested length), round-robin to the next AG when one
// is full. The rotor makes consecutive appends physically contiguous
// — the predecessor-merge in the extent paths then keeps almost
// every file at 1–2 extents.
//
// Runs never span AGs: each AG's always-allocated bitmap block caps
// any free run at the AG boundary, so free_extent can require its
// range to live in one AG.
//
// Callers hold an open transaction; every granted/freed range edits
// exactly one AG header block (txn) plus the in-memory advisory
// sb.free_blocks (written back by the op bracket's sb_write).

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

static u64 ag_block_of(const struct CarafsMount *m, u32 ag)
{
    return m->sb.ag_first_block + (u64)ag * m->sb.ag_size_blocks;
}

// Pin + verify an AG header block.
static int ag_get(struct CarafsMount *m, u32 ag, u8 **blk_out)
{
    u64 block = ag_block_of(m, ag);
    bool fresh;
    u8 *blk;
    int rc = carafs_cache_get(m, block, CARAFS_GET_READ, &blk, &fresh);
    if (rc != CARA_EOK) {
        return rc;
    }
    const struct CarafsAgHeader *agh = (const struct CarafsAgHeader *)blk;
    if (fresh) {
        u32 stored;
        memcpy(&stored, blk + offsetof(struct CarafsAgHeader, crc32c), 4);
        if (agh->magic != CARAFS_MAGIC_BITMAP || agh->block_no != block ||
            stored !=
                Carafs_BlockCrc(blk, m->block_size, offsetof(struct CarafsAgHeader, crc32c))) {
            carafs_cache_put(m, block);
            return CARA_EBADMAGIC;
        }
    }
    *blk_out = blk;
    return CARA_EOK;
}

static bool bit_get(const u8 *bits, u32 idx)
{
    return (bits[idx / 8] >> (idx % 8)) & 1u;
}

static void bit_set(u8 *bits, u32 idx)
{
    bits[idx / 8] |= (u8)(1u << (idx % 8));
}

static void bit_clear(u8 *bits, u32 idx)
{
    bits[idx / 8] &= (u8) ~(1u << (idx % 8));
}

// First free bit at or after `from` (no wrap); ag_size when none.
static u32 scan_free(const u8 *bits, u32 from, u32 ag_size)
{
    u32 i = from;
    while (i < ag_size) {
        if ((i % 8) == 0 && bits[i / 8] == 0xFF) {
            i += 8;
            continue;
        }
        if (!bit_get(bits, i)) {
            return i;
        }
        i++;
    }
    return ag_size;
}

// Try one AG: first free run from its rotor (wrapping once), up to
// `want` blocks. CARA_ENOTFOUND when the AG has nothing.
static int ag_try_alloc(struct CarafsMount *m, u32 ag, u32 want, u64 *start_out, u32 *count_out)
{
    u64 ag_block = ag_block_of(m, ag);
    u8 *blk;
    int rc = ag_get(m, ag, &blk);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsAgHeader *agh = (struct CarafsAgHeader *)blk;
    u8 *bits = blk + CARAFS_AG_BITS_OFF;
    u32 ag_size = m->sb.ag_size_blocks;
    if (agh->free_count == 0) {
        carafs_cache_put(m, ag_block);
        return CARA_ENOTFOUND;
    }
    u32 rotor = agh->rotor < ag_size ? agh->rotor : 0;
    u32 rel = scan_free(bits, rotor, ag_size);
    if (rel == ag_size) {
        rel = scan_free(bits, 0, ag_size); // wrap
        if (rel == ag_size) {
            carafs_cache_put(m, ag_block); // free_count lied — corrupt
            return CARA_EBADMAGIC;
        }
    }
    u32 got = 1;
    while (got < want && rel + got < ag_size && !bit_get(bits, rel + got)) {
        got++;
    }
    for (u32 i = 0; i < got; i++) {
        bit_set(bits, rel + i);
    }
    agh->free_count -= got;
    agh->rotor = (rel + got < ag_size) ? rel + got : 0;
    carafs_put_crc(blk, m->block_size, offsetof(struct CarafsAgHeader, crc32c));
    rc = carafs_txn_dirty(m, ag_block);
    carafs_cache_put(m, ag_block);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->sb.free_blocks -= got;
    // A previously freed block may still be cached (clean or dirty);
    // that stale image must not satisfy a later hit.
    for (u32 i = 0; i < got; i++) {
        carafs_cache_invalidate(m, ag_block + rel + i);
    }
    *start_out = ag_block + rel;
    *count_out = got;
    return CARA_EOK;
}

[[nodiscard]] int carafs_alloc_extent(struct CarafsMount *m, u64 hint, u32 want, u64 *start_out,
                                      u32 *count_out)
{
    if (want == 0) {
        return CARA_EINVAL;
    }
    u32 ag_count = m->sb.ag_count;
    u32 ag0 = 0;
    if (hint >= m->sb.ag_first_block && hint < m->sb.backup_sb) {
        ag0 = (u32)((hint - m->sb.ag_first_block) / m->sb.ag_size_blocks);
    }
    for (u32 i = 0; i < ag_count; i++) {
        u32 ag = (ag0 + i) % ag_count;
        int rc = ag_try_alloc(m, ag, want, start_out, count_out);
        if (rc != CARA_ENOTFOUND) {
            return rc; // success or a real error
        }
    }
    return CARA_ENOMEM; // volume full
}

[[nodiscard]] int carafs_alloc_block(struct CarafsMount *m, u64 hint, u64 *block_out)
{
    u32 count;
    return carafs_alloc_extent(m, hint, 1, block_out, &count);
}

[[nodiscard]] int carafs_free_extent(struct CarafsMount *m, u64 start, u32 count)
{
    if (count == 0 || start < m->sb.ag_first_block || start + count > m->sb.backup_sb) {
        return CARA_EINVAL;
    }
    u32 ag = (u32)((start - m->sb.ag_first_block) / m->sb.ag_size_blocks);
    u64 ag_block = ag_block_of(m, ag);
    u32 rel = (u32)(start - ag_block);
    if (rel == 0 || rel + count > m->sb.ag_size_blocks) {
        return CARA_EINVAL; // bitmap block itself / crosses the AG
    }
    u8 *blk;
    int rc = ag_get(m, ag, &blk);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsAgHeader *agh = (struct CarafsAgHeader *)blk;
    u8 *bits = blk + CARAFS_AG_BITS_OFF;
    for (u32 i = 0; i < count; i++) {
        if (!bit_get(bits, rel + i)) {
            carafs_cache_put(m, ag_block);
            return CARA_EINVAL; // double free
        }
    }
    for (u32 i = 0; i < count; i++) {
        bit_clear(bits, rel + i);
    }
    agh->free_count += count;
    carafs_put_crc(blk, m->block_size, offsetof(struct CarafsAgHeader, crc32c));
    rc = carafs_txn_dirty(m, ag_block);
    carafs_cache_put(m, ag_block);
    if (rc != CARA_EOK) {
        return rc;
    }
    m->sb.free_blocks += count;
    return CARA_EOK;
}
