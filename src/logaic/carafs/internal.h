// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS core internals — shared across src/logaic/carafs/, not
// exported. Portable: hosted builds get the libc prototypes, the
// freestanding kernel build declares the mem* trio the kernel's
// string.c provides (the compiler emits calls to them either way).

#ifndef CARAFS_INTERNAL_H
#define CARAFS_INTERNAL_H

#include <cara/carafs.h>
#include <cara/types.h>

#if __has_include(<string.h>)
#include <string.h>
#else
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#endif

constexpr u32 CARAFS_ENT_NONE = 0xFFFFFFFFu;
constexpr u64 CARAFS_BLOCK_NONE = ~0ull;

// Cache entry flags.
enum : u32 {
    CARAFS_ENT_DIRTY = (1u << 0), // differs from disk; needs home write
    CARAFS_ENT_TXN = (1u << 1),   // member of the open txn; not evictable
};

// cache_get modes.
enum : u32 {
    CARAFS_GET_READ = 0, // fill from bdev on miss
    CARAFS_GET_ZERO = 1, // fresh zeroed block on miss (allocation paths)
};

// Look up / load `block`, pin it, hand back its data pointer. Pairs
// with cache_put. Pinned entries never move or evict. *fresh_out
// (nullable) reports whether the data was (re)loaded from the bdev
// this call — typed getters verify magic/crc only then.
[[nodiscard]] int carafs_cache_get(struct CarafsMount *m, u64 block, u32 mode, u8 **data_out,
                                   bool *fresh_out);
void carafs_cache_put(struct CarafsMount *m, u64 block);

// Mark a cached block dirty WITHOUT joining the open txn — file data
// blocks only (lazy writeback; metadata goes through txn_dirty).
void carafs_cache_dirty(struct CarafsMount *m, u64 block);

// Drop an unpinned, non-txn entry. Called by the allocator on every
// block it grants: a stale cached image of a previously freed block
// must not satisfy a later CARAFS_GET_ZERO hit.
void carafs_cache_invalidate(struct CarafsMount *m, u64 block);

// Write back every dirty entry + bdev flush.
[[nodiscard]] int carafs_cache_sync(struct CarafsMount *m);

// Transactions: bracket every mutating operation. txn_dirty marks a
// cached (and pinned-by-caller) block as part of the txn. Pre-F4,
// commit writes the txn blocks home and flushes; F4 turns commit
// into a WAL append and makes home writes lazy.
void carafs_txn_begin(struct CarafsMount *m);
[[nodiscard]] int carafs_txn_dirty(struct CarafsMount *m, u64 block);
[[nodiscard]] int carafs_txn_commit(struct CarafsMount *m);
void carafs_txn_abort(struct CarafsMount *m);

// Serialize m->sb into the cached block 0 and add it to the txn.
[[nodiscard]] int carafs_sb_write(struct CarafsMount *m);

// First-write hook: flip the superblock CLEAN → DIRTY durably before
// any other metadata write of this mount touches disk.
[[nodiscard]] int carafs_mark_dirty(struct CarafsMount *m);

// Mutating-operation bracket: begin = writability check +
// mark_dirty + txn_begin + sb snapshot; commit = sb_write +
// txn_commit; abort = txn_abort + sb restore. Every public mutator
// is exactly one bracket.
[[nodiscard]] int carafs_op_begin(struct CarafsMount *m);
[[nodiscard]] int carafs_op_commit(struct CarafsMount *m);
void carafs_op_abort(struct CarafsMount *m);

// ---- Allocator (alloc.c) -----------------------------------------------------

// Allocate up to `want` contiguous blocks: first free run from the
// rotor of the AG containing `hint` (0 → AG 0), round-robin to the
// other AGs when full. Grants a single run (callers loop); updates
// the AG header (txn) and the advisory sb free count, and
// invalidates stale cache entries for the granted blocks. Returns
// CARA_ENOMEM when the volume is full.
[[nodiscard]] int carafs_alloc_extent(struct CarafsMount *m, u64 hint, u32 want, u64 *start_out,
                                      u32 *count_out);
[[nodiscard]] int carafs_alloc_block(struct CarafsMount *m, u64 hint, u64 *block_out);
[[nodiscard]] int carafs_free_extent(struct CarafsMount *m, u64 start, u32 count);

// ---- Cnodes (cnode.c) --------------------------------------------------------

// Pin + verify a cnode block (magic, crc-on-load, self-address).
// CARA_ENOENT for a tombstone (link_count 0), CARA_EBADMAGIC for a
// non-cnode. Pairs with cnode_put; dirty = re-crc + txn_dirty.
[[nodiscard]] int carafs_cnode_get(struct CarafsMount *m, u64 block, struct CarafsCnode **out);
void carafs_cnode_put(struct CarafsMount *m, struct CarafsCnode *cn);
[[nodiscard]] int carafs_cnode_dirty(struct CarafsMount *m, struct CarafsCnode *cn);

// Lifecycle core without the op bracket — for callers (dir.c) already
// inside a transaction. alloc grants + initialises a cnode (link_count
// 1, generation recovered from a tombstone); free_locked releases a
// pinned cnode's storage and rewrites it as a tombstone (caller puts).
[[nodiscard]] int carafs_cnode_alloc(struct CarafsMount *m, u16 type, u64 hint, u64 *block_out);
[[nodiscard]] int carafs_cnode_free_locked(struct CarafsMount *m, struct CarafsCnode *cn);

// TLV item area. resize creates/grows/shrinks `kind` in place
// (growth zero-filled, later records repacked) and returns the
// payload; CARA_EOVERFLOW when the area is full.
[[nodiscard]] u8 *carafs_item_find(struct CarafsCnode *cn, u32 bs, u16 kind, u16 *len_out);
[[nodiscard]] int carafs_item_resize(struct CarafsCnode *cn, u32 bs, u16 kind, u16 new_len,
                                     u8 **payload_out);
void carafs_item_remove(struct CarafsCnode *cn, u32 bs, u16 kind);
[[nodiscard]] u32 carafs_item_room(const struct CarafsCnode *cn, u32 bs);

// ---- Extent B+tree (btree.c) -------------------------------------------------
//
// CARAFS_BT_EXTENT flavour: fixed 24-byte records at every level
// (CarafsExtentRec leaves, CarafsBtInteriorRec interior), keyed by
// file block offset. F3 grows the DIR flavour onto the same nodes.

// Move the cnode's inline extents into a fresh tree root leaf
// (holes — start == 0 runs — are dropped; the tree encodes holes as
// absent keys). Sets cn->tree_root, clears the inline array.
[[nodiscard]] int carafs_etree_spill(struct CarafsMount *m, struct CarafsCnode *cn);

// Greatest record with file_off <= fblk / smallest with
// file_off > fblk. CARA_ENOTFOUND when no such record.
[[nodiscard]] int carafs_etree_floor(struct CarafsMount *m, u64 root, u64 fblk,
                                     struct CarafsExtentRec *out);
[[nodiscard]] int carafs_etree_next(struct CarafsMount *m, u64 root, u64 fblk,
                                    struct CarafsExtentRec *out);

// Insert a record (merges with a physically-adjacent predecessor;
// splits leaves/interiors and grows the root as needed — may update
// cn->tree_root).
[[nodiscard]] int carafs_etree_insert(struct CarafsMount *m, struct CarafsCnode *cn,
                                      const struct CarafsExtentRec *rec);

// Free every extent the tree maps plus the tree's own node blocks;
// clears cn->tree_root.
[[nodiscard]] int carafs_etree_free_all(struct CarafsMount *m, struct CarafsCnode *cn);

// ---- Directory B+tree (btree.c, CARAFS_BT_DIR flavour) -----------------------
//
// Variable-stride CarafsDirent leaves keyed by (name_hash, seq); shares
// the interior machinery with the extent tree. dir.c drives promotion
// (inline item -> single leaf -> tree) and the cnode-level glue.

// Scan the equal-hash run for `name`: *exists reports a folded-name
// match (header copied to *match when non-null); *free_seq (non-null)
// gets the smallest unused collision_seq for a new insert.
[[nodiscard]] int carafs_dtree_scan(struct CarafsMount *m, u64 root, u64 hash, const void *name,
                                    u32 name_len, bool *exists, struct CarafsDirent *match,
                                    u8 *free_seq);

// Folded-name lookup. CARA_EOK + *out on hit, CARA_ENOTFOUND otherwise.
[[nodiscard]] int carafs_dtree_lookup(struct CarafsMount *m, u64 root, u64 hash, const void *name,
                                      u32 name_len, struct CarafsDirent *out);

// Forward iteration: first entry with key > (hash, seq) (strict) or >=
// (otherwise). Header to *out, name bytes to name_out (>= 255 bytes).
[[nodiscard]] int carafs_dtree_next(struct CarafsMount *m, u64 root, u64 hash, u8 seq, bool strict,
                                    struct CarafsDirent *out, u8 *name_out);

// Insert a fully-formed dirent whose key (hash, seq) is unique; `name`
// holds de->name_len bytes. Splits + grows the tree as needed.
[[nodiscard]] int carafs_dtree_insert(struct CarafsMount *m, struct CarafsCnode *cn,
                                      const struct CarafsDirent *de, const void *name);

// Remove the entry keyed (hash, seq); frees emptied nodes (and the whole
// tree when the last entry goes). CARA_ENOTFOUND if absent.
[[nodiscard]] int carafs_dtree_remove(struct CarafsMount *m, struct CarafsCnode *cn, u64 hash,
                                      u8 seq);

// Move a packed, sorted inline-dirent blob (`bytes` long) into a fresh
// single leaf; sets cn->tree_root (caller drops the inline item).
[[nodiscard]] int carafs_dtree_spill(struct CarafsMount *m, struct CarafsCnode *cn, const u8 *blob,
                                     u32 bytes);

// Free every node block of the directory tree; clears cn->tree_root.
[[nodiscard]] int carafs_dtree_free_all(struct CarafsMount *m, struct CarafsCnode *cn);

// Geometry helpers shared by mkfs / fsck / allocator.
[[nodiscard]] static inline u32 carafs_ag_size(u32 block_size)
{
    return 8u * (block_size - CARAFS_AG_BITS_OFF);
}

// Recompute and store a metadata block's crc — call after the last
// mutation, before txn_dirty / bdev write.
static inline void carafs_put_crc(u8 *block, u32 bs, u32 crc_off)
{
    u32 crc = Carafs_BlockCrc(block, bs, crc_off);
    memcpy(block + crc_off, &crc, 4);
}

struct CarafsGeometry {
    u32 block_size;
    u64 total_blocks;
    u64 journal_start; // == 1
    u32 journal_blocks;
    u64 ag_first;
    u32 ag_size;
    u32 ag_count;
    u64 backup_sb; // == total_blocks - 1
    u64 root_cnode;
};

// Compute the canonical layout for a device. Returns CARA_ERANGE if
// the device is too small to hold sb + journal + one useful AG +
// backup sb.
[[nodiscard]] int carafs_geometry(struct CarafsGeometry *g, u32 block_size, u64 total_blocks);

// now_ns through the mount's clock (0 when none was provided).
[[nodiscard]] static inline u64 carafs_now(const struct CarafsMount *m)
{
    return m->now_ns ? m->now_ns(m->now_ctx) : 0;
}

[[nodiscard]] static inline u64 carafs_min_u64(u64 a, u64 b)
{
    return a < b ? a : b;
}

[[nodiscard]] static inline u64 carafs_max_u64(u64 a, u64 b)
{
    return a > b ? a : b;
}

#endif
