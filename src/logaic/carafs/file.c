// SPDX-License-Identifier: BSD-2-Clause
//
// File content read/write (F2; CARAFS.md §3.4/§3.7).
//
// A file's storage escalates transparently: INLINE_DATA item in the
// cnode (tiny files, one block total) → the 16 inline extents →
// the extent B+tree. Inline extents carry no file offset: they are
// concatenated runs, with start == 0 encoding a hole (block 0 is the
// superblock — never a valid extent start). The tree keys records by
// file offset and encodes holes as absent keys. Invariants: the last
// inline extent is never a hole, and a file is inline-data XOR
// extent-mapped.
//
// File data blocks go through the cache as plain dirty entries —
// written back on eviction, sync, or unmount, NOT as part of the
// metadata transaction (txn capacity is for metadata; F4's
// ordered-data discipline will sequence data before commit).

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

// One contiguous mapped (or hole) run starting at a file block.
struct FileMap {
    u64 phys; // 0 = hole
    u64 run;  // blocks from the queried offset
    bool unwritten;
    bool fresh; // freshly allocated by this call (content undefined)
};

// ---- Mapping lookup -----------------------------------------------------------

static int map_lookup(struct CarafsMount *m, const struct CarafsCnode *cn, u64 fblk,
                      struct FileMap *map)
{
    *map = (struct FileMap){ .run = ~0ull };
    if (cn->tree_root != 0) {
        struct CarafsExtentRec rec;
        int rc = carafs_etree_floor(m, cn->tree_root, fblk, &rec);
        if (rc == CARA_EOK && fblk < rec.file_off + rec.count) {
            map->phys = rec.start + (fblk - rec.file_off);
            map->run = rec.file_off + rec.count - fblk;
            map->unwritten = (rec.flags & CARAFS_EXT_UNWRITTEN) != 0;
            return CARA_EOK;
        }
        if (rc != CARA_EOK && rc != CARA_ENOTFOUND) {
            return rc;
        }
        // Hole: bounded by the next mapped extent, if any.
        rc = carafs_etree_next(m, cn->tree_root, fblk, &rec);
        if (rc == CARA_EOK) {
            map->run = rec.file_off - fblk;
        } else if (rc != CARA_ENOTFOUND) {
            return rc;
        }
        return CARA_EOK;
    }
    u64 fo = 0;
    for (u32 i = 0; i < cn->n_inline_extents; i++) {
        u64 end = fo + cn->ext[i].count;
        if (fblk < end) {
            map->run = end - fblk;
            if (cn->ext[i].start != 0) {
                map->phys = cn->ext[i].start + (fblk - fo);
                map->unwritten = (cn->ext[i].flags & CARAFS_EXT_UNWRITTEN) != 0;
            }
            return CARA_EOK;
        }
        fo = end;
    }
    return CARA_EOK; // past the mapped end: unbounded hole
}

// ---- Inline-extent mutation ----------------------------------------------------

static u64 inline_mapped_end(const struct CarafsCnode *cn)
{
    u64 fo = 0;
    for (u32 i = 0; i < cn->n_inline_extents; i++) {
        fo += cn->ext[i].count;
    }
    return fo;
}

// Record a fresh data run at file block `fo` in the inline array,
// spilling to the tree when the array can't take the shape.
static int inline_insert(struct CarafsMount *m, struct CarafsCnode *cn, u64 fo, u64 start,
                         u32 count)
{
    const struct CarafsExtentRec rec = { .file_off = fo, .start = start, .count = count };
    u32 n = cn->n_inline_extents;
    u64 end = inline_mapped_end(cn);

    if (fo == end) {
        // Append: merge into a physically-adjacent last extent.
        if (n > 0 && cn->ext[n - 1].start != 0 &&
            cn->ext[n - 1].start + cn->ext[n - 1].count == start &&
            (u64)cn->ext[n - 1].count + count <= 0xFFFFFFFFull) {
            cn->ext[n - 1].count += count;
            return CARA_EOK;
        }
        if (n == CARAFS_INLINE_EXTENTS) {
            int rc = carafs_etree_spill(m, cn);
            if (rc != CARA_EOK) {
                return rc;
            }
            return carafs_etree_insert(m, cn, &rec);
        }
        cn->ext[n] = (struct CarafsExtent){ .start = start, .count = count };
        cn->n_inline_extents = n + 1;
        return CARA_EOK;
    }
    if (fo > end) {
        // Sparse append: an explicit hole run, then the data.
        u64 gap = fo - end;
        if (n + 2 > CARAFS_INLINE_EXTENTS || gap > 0xFFFFFFFFull) {
            int rc = carafs_etree_spill(m, cn);
            if (rc != CARA_EOK) {
                return rc;
            }
            return carafs_etree_insert(m, cn, &rec);
        }
        cn->ext[n] = (struct CarafsExtent){ .start = 0, .count = (u32)gap };
        cn->ext[n + 1] = (struct CarafsExtent){ .start = start, .count = count };
        cn->n_inline_extents = n + 2;
        return CARA_EOK;
    }
    // fo < end: filling (part of) an interior hole run. Writes only
    // land on holes — the caller never re-allocates mapped blocks.
    u64 hs = 0;
    u32 i = 0;
    for (; i < n; i++) {
        if (fo < hs + cn->ext[i].count) {
            break;
        }
        hs += cn->ext[i].count;
    }
    if (i == n || cn->ext[i].start != 0 || fo + count > hs + cn->ext[i].count) {
        return CARA_EINVAL; // not a hole: mapping logic error
    }
    u32 pre = (u32)(fo - hs);
    u32 post = (u32)(hs + cn->ext[i].count - (fo + count));
    u32 need = (pre ? 1u : 0u) + 1u + (post ? 1u : 0u);
    if (n - 1 + need > CARAFS_INLINE_EXTENTS) {
        int rc = carafs_etree_spill(m, cn);
        if (rc != CARA_EOK) {
            return rc;
        }
        return carafs_etree_insert(m, cn, &rec);
    }
    memmove(&cn->ext[i + need], &cn->ext[i + 1], (n - i - 1) * sizeof(struct CarafsExtent));
    u32 w = i;
    if (pre) {
        cn->ext[w++] = (struct CarafsExtent){ .start = 0, .count = pre };
    }
    cn->ext[w++] = (struct CarafsExtent){ .start = start, .count = count };
    if (post) {
        cn->ext[w++] = (struct CarafsExtent){ .start = 0, .count = post };
    }
    cn->n_inline_extents = n - 1 + need;
    return CARA_EOK;
}

// Map file block `fblk`, allocating up to `want` blocks when it is a
// hole. Returns one run; callers loop.
static int ensure_mapped(struct CarafsMount *m, struct CarafsCnode *cn, u64 fblk, u64 want,
                         struct FileMap *map)
{
    int rc = map_lookup(m, cn, fblk, map);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (map->phys != 0) {
        map->run = carafs_min_u64(map->run, want);
        return CARA_EOK;
    }
    u32 ask = (u32)carafs_min_u64(carafs_min_u64(want, map->run), 0x80000000ull);
    u64 start;
    u32 got;
    rc = carafs_alloc_extent(m, cn->block_no, ask, &start, &got);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (cn->tree_root != 0) {
        const struct CarafsExtentRec rec = { .file_off = fblk, .start = start, .count = got };
        rc = carafs_etree_insert(m, cn, &rec);
    } else {
        rc = inline_insert(m, cn, fblk, start, got);
    }
    if (rc != CARA_EOK) {
        return rc;
    }
    cn->blocks_used += got;
    *map = (struct FileMap){ .phys = start, .run = got, .fresh = true };
    return CARA_EOK;
}

// ---- Block-mapped write -------------------------------------------------------

// Copy `len` bytes from `src` into the file's blocks at byte offset
// `off`, allocating as needed. `old_size` bounds where existing data
// must be preserved (read-modify-write) vs zero-filled.
static int blocks_write(struct CarafsMount *m, struct CarafsCnode *cn, u64 off, const u8 *src,
                        usize len, u64 old_size)
{
    u32 bs = m->block_size;
    u64 last_blk = (off + len - 1) / bs;
    while (len > 0) {
        u64 fblk = off / bs;
        struct FileMap map;
        int rc = ensure_mapped(m, cn, fblk, last_blk - fblk + 1, &map);
        if (rc != CARA_EOK) {
            return rc;
        }
        for (u64 b = 0; b < map.run && len > 0; b++) {
            u32 boff = (u32)(off % bs);
            u32 chunk = (u32)carafs_min_u64(bs - boff, len);
            // Fresh allocations and whole-block writes need no disk
            // read; so do blocks wholly past the old EOF.
            bool zero = map.fresh || chunk == bs || (fblk + b) * (u64)bs >= old_size;
            u8 *data;
            rc = carafs_cache_get(m, map.phys + b, zero ? CARAFS_GET_ZERO : CARAFS_GET_READ, &data,
                                  nullptr);
            if (rc != CARA_EOK) {
                return rc;
            }
            memcpy(data + boff, src, chunk);
            carafs_cache_dirty(m, map.phys + b);
            carafs_cache_put(m, map.phys + b);
            off += chunk;
            src += chunk;
            len -= chunk;
        }
    }
    return CARA_EOK;
}

// ---- Public API -----------------------------------------------------------------

[[nodiscard]] int Carafs_FileWrite(struct CarafsMount *m, u64 cnode, u64 off, const void *buf,
                                   usize len)
{
    if (!m || !buf) {
        return CARA_EINVAL;
    }
    if (len == 0) {
        return CARA_EOK;
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
    if (cn->type != CARAFS_T_FILE) {
        rc = CARA_EINVAL;
        goto fail;
    }
    u64 old_size = cn->size_bytes;
    u64 new_size = carafs_max_u64(old_size, off + len);
    bool extent_mapped = cn->tree_root != 0 || cn->n_inline_extents != 0;

    if (!extent_mapped) {
        // Inline-data path: grow the item in place while it fits.
        u8 *payload;
        if (new_size <= 0xFFFFull && carafs_item_resize(cn, m->block_size, CARAFS_ITEM_INLINE_DATA,
                                                        (u16)new_size, &payload) == CARA_EOK) {
            memcpy(payload + off, buf, len);
            goto done;
        }
        // Promote: stream the inline bytes out to real blocks, then
        // drop the item. The payload stays valid (it lives in the
        // pinned cnode block; blocks_write touches other blocks).
        u16 cur_len;
        u8 *cur = carafs_item_find(cn, m->block_size, CARAFS_ITEM_INLINE_DATA, &cur_len);
        if (cur && cur_len > 0) {
            rc = blocks_write(m, cn, 0, cur, cur_len, 0);
            if (rc != CARA_EOK) {
                goto fail;
            }
        }
        carafs_item_remove(cn, m->block_size, CARAFS_ITEM_INLINE_DATA);
    }
    rc = blocks_write(m, cn, off, buf, len, old_size);
    if (rc != CARA_EOK) {
        goto fail;
    }
done:
    cn->size_bytes = new_size;
    cn->modified_ns = carafs_now(m);
    cn->changed_ns = cn->modified_ns;
    rc = carafs_cnode_dirty(m, cn);
    carafs_cnode_put(m, cn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    return carafs_op_commit(m);
fail:
    carafs_cnode_put(m, cn);
    carafs_op_abort(m);
    return rc;
}

[[nodiscard]] int Carafs_FileRead(struct CarafsMount *m, u64 cnode, u64 off, void *buf, usize len,
                                  usize *read_out)
{
    if (!m || !m->mounted || !buf || !read_out) {
        return CARA_EINVAL;
    }
    *read_out = 0;
    struct CarafsCnode *cn;
    int rc = carafs_cnode_get(m, cnode, &cn);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (cn->type != CARAFS_T_FILE) {
        carafs_cnode_put(m, cn);
        return CARA_EINVAL;
    }
    u64 size = cn->size_bytes;
    if (off >= size) {
        carafs_cnode_put(m, cn);
        return CARA_EOK;
    }
    usize todo = (usize)carafs_min_u64(len, size - off);
    usize total = todo;
    u8 *dst = buf;

    u16 inl_len;
    const u8 *inl = carafs_item_find(cn, m->block_size, CARAFS_ITEM_INLINE_DATA, &inl_len);
    if (inl) {
        memcpy(dst, inl + off, todo); // inline invariant: size == item len
        carafs_cnode_put(m, cn);
        *read_out = todo;
        return CARA_EOK;
    }

    u32 bs = m->block_size;
    while (todo > 0) {
        u64 fblk = off / bs;
        u32 boff = (u32)(off % bs);
        u32 chunk = (u32)carafs_min_u64(bs - boff, todo);
        struct FileMap map;
        rc = map_lookup(m, cn, fblk, &map);
        if (rc != CARA_EOK) {
            carafs_cnode_put(m, cn);
            return rc;
        }
        if (map.phys == 0 || map.unwritten) {
            memset(dst, 0, chunk);
        } else {
            u8 *data;
            rc = carafs_cache_get(m, map.phys, CARAFS_GET_READ, &data, nullptr);
            if (rc != CARA_EOK) {
                carafs_cnode_put(m, cn);
                return rc;
            }
            memcpy(dst, data + boff, chunk);
            carafs_cache_put(m, map.phys);
        }
        off += chunk;
        dst += chunk;
        todo -= chunk;
    }
    carafs_cnode_put(m, cn);
    *read_out = total;
    return CARA_EOK;
}
