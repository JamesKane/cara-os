// SPDX-License-Identifier: BSD-2-Clause
//
// Cnode lifecycle + the TLV item area (F2; CARAFS.md §3.4).
//
// A cnode is one block; its id is its block number. The fixed header
// (struct CarafsCnode) is followed by packed TLV items to the end of
// the block — name, comment, inline data, inline dirents… — at
// 8-byte record stride. F2 uses INLINE_DATA; F3 adds the rest.
//
// Generations: creation reads the granted block first and, when a
// valid (tombstoned) cnode is still legible there, continues its
// generation sequence — a dangling reference to the old object then
// fails the generation check instead of silently resolving. Reuse as
// a data block destroys the tombstone; the discipline is best-effort
// by design (§3.1).

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

[[nodiscard]] int carafs_cnode_get(struct CarafsMount *m, u64 block, struct CarafsCnode **out)
{
    bool fresh;
    u8 *blk;
    int rc = carafs_cache_get(m, block, CARAFS_GET_READ, &blk, &fresh);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsCnode *cn = (struct CarafsCnode *)blk;
    if (cn->magic != CARAFS_MAGIC_CNODE || cn->block_no != block) {
        carafs_cache_put(m, block);
        return CARA_EBADMAGIC;
    }
    if (fresh) {
        u32 stored;
        memcpy(&stored, blk + offsetof(struct CarafsCnode, crc32c), 4);
        if (stored != Carafs_BlockCrc(blk, m->block_size, offsetof(struct CarafsCnode, crc32c))) {
            carafs_cache_put(m, block);
            return CARA_EBADMAGIC;
        }
    }
    if (cn->link_count == 0) {
        carafs_cache_put(m, block);
        return CARA_ENOENT; // tombstone
    }
    *out = cn;
    return CARA_EOK;
}

void carafs_cnode_put(struct CarafsMount *m, struct CarafsCnode *cn)
{
    carafs_cache_put(m, cn->block_no);
}

[[nodiscard]] int carafs_cnode_dirty(struct CarafsMount *m, struct CarafsCnode *cn)
{
    carafs_put_crc((u8 *)cn, m->block_size, offsetof(struct CarafsCnode, crc32c));
    return carafs_txn_dirty(m, cn->block_no);
}

// ---- TLV item area ------------------------------------------------------------

[[nodiscard]] u32 carafs_item_room(const struct CarafsCnode *cn, u32 bs)
{
    return bs - CARAFS_CNODE_ITEMS_OFF - cn->item_bytes;
}

// Byte offset of `kind` within the item area; item_bytes when absent.
static u32 item_off(const struct CarafsCnode *cn, u16 kind)
{
    const u8 *area = (const u8 *)cn + CARAFS_CNODE_ITEMS_OFF;
    u32 off = 0;
    while (off < cn->item_bytes) {
        const struct CarafsItem *it = (const struct CarafsItem *)(area + off);
        if (it->kind == kind) {
            return off;
        }
        off += Carafs_Align8(sizeof(struct CarafsItem) + it->len);
    }
    return cn->item_bytes;
}

[[nodiscard]] u8 *carafs_item_find(struct CarafsCnode *cn, u32 bs, u16 kind, u16 *len_out)
{
    (void)bs;
    u32 off = item_off(cn, kind);
    if (off == cn->item_bytes) {
        return nullptr;
    }
    u8 *area = (u8 *)cn + CARAFS_CNODE_ITEMS_OFF;
    struct CarafsItem *it = (struct CarafsItem *)(area + off);
    if (len_out) {
        *len_out = it->len;
    }
    return area + off + sizeof(struct CarafsItem);
}

[[nodiscard]] int carafs_item_resize(struct CarafsCnode *cn, u32 bs, u16 kind, u16 new_len,
                                     u8 **payload_out)
{
    u8 *area = (u8 *)cn + CARAFS_CNODE_ITEMS_OFF;
    u32 off = item_off(cn, kind);
    u32 stride_new = Carafs_Align8(sizeof(struct CarafsItem) + new_len);
    u32 old_len = 0;
    u32 stride_old = 0;
    if (off < cn->item_bytes) {
        struct CarafsItem *it = (struct CarafsItem *)(area + off);
        old_len = it->len;
        stride_old = Carafs_Align8(sizeof(struct CarafsItem) + old_len);
    }
    if (cn->item_bytes - stride_old + stride_new > bs - CARAFS_CNODE_ITEMS_OFF) {
        return CARA_EOVERFLOW;
    }
    if (stride_old != stride_new) {
        // Repack the records after this one (or open the gap).
        memmove(area + off + stride_new, area + off + stride_old,
                cn->item_bytes - off - stride_old);
        cn->item_bytes += stride_new - stride_old;
    }
    struct CarafsItem *it = (struct CarafsItem *)(area + off);
    it->kind = kind;
    it->len = new_len;
    u8 *payload = area + off + sizeof(struct CarafsItem);
    if (new_len > old_len) {
        memset(payload + old_len, 0, new_len - old_len);
    }
    // Keep the pad slack deterministic (it is crc'd with the block).
    memset(payload + new_len, 0, stride_new - sizeof(struct CarafsItem) - new_len);
    *payload_out = payload;
    return CARA_EOK;
}

void carafs_item_remove(struct CarafsCnode *cn, u32 bs, u16 kind)
{
    u8 *area = (u8 *)cn + CARAFS_CNODE_ITEMS_OFF;
    u32 off = item_off(cn, kind);
    if (off == cn->item_bytes) {
        return;
    }
    struct CarafsItem *it = (struct CarafsItem *)(area + off);
    u32 stride = Carafs_Align8(sizeof(struct CarafsItem) + it->len);
    memmove(area + off, area + off + stride, cn->item_bytes - off - stride);
    cn->item_bytes -= stride;
    // Zero the vacated tail so the block image stays deterministic.
    memset(area + cn->item_bytes, 0, stride);
    (void)bs;
}

// ---- Lifecycle core (no op bracket; caller is mid-transaction) ----------------
//
// Carafs_CnodeCreate/Delete wrap these in their own op bracket; the
// directory layer (dir.c) calls them directly inside its own bracket so
// the cnode + its dirent commit atomically.

[[nodiscard]] int carafs_cnode_alloc(struct CarafsMount *m, u16 type, u64 hint, u64 *block_out)
{
    u64 block;
    int rc = carafs_alloc_block(m, hint, &block);
    if (rc != CARA_EOK) {
        return rc;
    }
    u8 *blk;
    rc = carafs_cache_get(m, block, CARAFS_GET_READ, &blk, nullptr);
    if (rc != CARA_EOK) {
        return rc;
    }
    // Generation recovery: continue a legible tombstone's sequence.
    u64 generation = 1;
    const struct CarafsCnode *old = (const struct CarafsCnode *)blk;
    if (old->magic == CARAFS_MAGIC_CNODE && old->block_no == block) {
        u32 stored;
        memcpy(&stored, blk + offsetof(struct CarafsCnode, crc32c), 4);
        if (stored == Carafs_BlockCrc(blk, m->block_size, offsetof(struct CarafsCnode, crc32c))) {
            generation = old->generation + 1;
        }
    }
    memset(blk, 0, m->block_size);
    struct CarafsCnode *cn = (struct CarafsCnode *)blk;
    u64 now = carafs_now(m);
    cn->magic = CARAFS_MAGIC_CNODE;
    cn->block_no = block;
    cn->generation = generation;
    cn->type = type;
    cn->link_count = 1;
    cn->created_ns = now;
    cn->modified_ns = now;
    cn->changed_ns = now;
    rc = carafs_cnode_dirty(m, cn);
    carafs_cache_put(m, block);
    if (rc != CARA_EOK) {
        return rc;
    }
    *block_out = block;
    return CARA_EOK;
}

// Free a cnode's storage and rewrite the block as a tombstone (gen+1,
// link_count 0). `cn` stays pinned; the caller puts it.
[[nodiscard]] int carafs_cnode_free_locked(struct CarafsMount *m, struct CarafsCnode *cn)
{
    int rc = CARA_EOK;
    u64 block = cn->block_no;
    // Free the storage: extent tree, then inline extents (start == 0
    // runs are holes), then the cnode block itself.
    if (cn->tree_root != 0 && cn->type == CARAFS_T_FILE) {
        rc = carafs_etree_free_all(m, cn);
    } else if (cn->tree_root != 0 && cn->type == CARAFS_T_DIR) {
        rc = carafs_dtree_free_all(m, cn);
    }
    for (u32 i = 0; rc == CARA_EOK && i < cn->n_inline_extents; i++) {
        if (cn->ext[i].start != 0) {
            rc = carafs_free_extent(m, cn->ext[i].start, cn->ext[i].count);
        }
    }
    if (rc == CARA_EOK) {
        rc = carafs_free_extent(m, block, 1);
    }
    if (rc != CARA_EOK) {
        return rc;
    }
    // Tombstone: bump the generation, drop the link count, clear the
    // rest. The block is free; the tombstone survives until reuse.
    u64 generation = cn->generation + 1;
    u16 type = cn->type;
    memset(cn, 0, m->block_size);
    cn->magic = CARAFS_MAGIC_CNODE;
    cn->block_no = block;
    cn->generation = generation;
    cn->type = type;
    cn->changed_ns = carafs_now(m);
    return carafs_cnode_dirty(m, cn);
}

// ---- Public lifecycle ----------------------------------------------------------

[[nodiscard]] int Carafs_CnodeCreate(struct CarafsMount *m, u16 type, u64 hint, u64 *cnode_out)
{
    if (!m || !cnode_out ||
        (type != CARAFS_T_FILE && type != CARAFS_T_DIR && type != CARAFS_T_SYMLINK)) {
        return CARA_EINVAL;
    }
    int rc = carafs_op_begin(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    u64 block;
    rc = carafs_cnode_alloc(m, type, hint, &block);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    rc = carafs_op_commit(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    *cnode_out = block;
    return CARA_EOK;
}

[[nodiscard]] int Carafs_CnodeDelete(struct CarafsMount *m, u64 cnode)
{
    if (!m) {
        return CARA_EINVAL;
    }
    int rc = carafs_op_begin(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsCnode *cn;
    rc = carafs_cnode_get(m, cnode, &cn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    rc = carafs_cnode_free_locked(m, cn);
    carafs_cnode_put(m, cn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    return carafs_op_commit(m);
}

[[nodiscard]] int Carafs_CnodeStat(struct CarafsMount *m, u64 cnode, struct CarafsStat *st)
{
    if (!m || !m->mounted || !st) {
        return CARA_EINVAL;
    }
    struct CarafsCnode *cn;
    int rc = carafs_cnode_get(m, cnode, &cn);
    if (rc != CARA_EOK) {
        return rc;
    }
    *st = (struct CarafsStat){
        .cnode = cnode,
        .generation = cn->generation,
        .type = cn->type,
        .link_count = cn->link_count,
        .size_bytes = cn->size_bytes,
        .blocks_used = cn->blocks_used,
        .parent_cnode = cn->parent_cnode,
        .created_ns = cn->created_ns,
        .modified_ns = cn->modified_ns,
        .changed_ns = cn->changed_ns,
        .fib_protection = cn->fib_protection,
        .flags = cn->flags,
    };
    carafs_cnode_put(m, cn);
    return CARA_EOK;
}
