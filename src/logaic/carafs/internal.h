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
// with cache_put. Pinned entries never move or evict.
[[nodiscard]] int carafs_cache_get(struct CarafsMount *m, u64 block, u32 mode, u8 **data_out);
void carafs_cache_put(struct CarafsMount *m, u64 block);

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

// Geometry helpers shared by mkfs / fsck / allocator.
[[nodiscard]] static inline u32 carafs_ag_size(u32 block_size)
{
    return 8u * (block_size - CARAFS_AG_BITS_OFF);
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
