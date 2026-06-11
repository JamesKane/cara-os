// SPDX-License-Identifier: BSD-2-Clause
//
// Extent-flavour B+tree (F2; CARAFS.md §3.7). One node format shared
// with the F3 directory tree (§3.6): interior nodes hold 24-byte
// (key, child) records whose key is the smallest key in the child's
// subtree; extent leaves hold 24-byte CarafsExtentRec sorted by file
// block offset. Holes are absent keys — the tree maps only real
// extents.
//
// Pin discipline: at most one tree node is pinned at a time; walks
// remember (block, index) per level and re-get ancestors (cache
// hits) instead of holding pins down the path. Mutators run inside
// the caller's open transaction; node blocks come from / return to
// the AG allocator.
//
// Insert never overlaps an existing extent (file.c only maps holes)
// and merges with a physically-adjacent predecessor, which keeps the
// rotor allocator's consecutive appends at one record.

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

// 169 records per 4 KiB node → depth 4 maps > 10^9 extents; 8 is
// unreachable headroom (and holds for 512 B nodes: 20^8 >> 2^32).
constexpr u32 BT_MAX_DEPTH = 8;

struct BtPathLvl {
    u64 block;
    u32 idx; // descent index taken at this (interior) level
};

static u32 bt_cap(const struct CarafsMount *m)
{
    return (m->block_size - CARAFS_BT_BODY_OFF) / sizeof(struct CarafsBtInteriorRec);
}

static struct CarafsBtInteriorRec *bt_irecs(struct CarafsBtNode *n)
{
    return (struct CarafsBtInteriorRec *)((u8 *)n + CARAFS_BT_BODY_OFF);
}

static struct CarafsExtentRec *bt_erecs(struct CarafsBtNode *n)
{
    return (struct CarafsExtentRec *)((u8 *)n + CARAFS_BT_BODY_OFF);
}

static int bt_get(struct CarafsMount *m, u64 block, struct CarafsBtNode **out)
{
    bool fresh;
    u8 *blk;
    int rc = carafs_cache_get(m, block, CARAFS_GET_READ, &blk, &fresh);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsBtNode *n = (struct CarafsBtNode *)blk;
    if (n->magic != CARAFS_MAGIC_BTREE || n->block_no != block || n->flavour != CARAFS_BT_EXTENT ||
        n->n_records > bt_cap(m)) {
        carafs_cache_put(m, block);
        return CARA_EBADMAGIC;
    }
    if (fresh) {
        u32 stored;
        memcpy(&stored, blk + offsetof(struct CarafsBtNode, crc32c), 4);
        if (stored != Carafs_BlockCrc(blk, m->block_size, offsetof(struct CarafsBtNode, crc32c))) {
            carafs_cache_put(m, block);
            return CARA_EBADMAGIC;
        }
    }
    *out = n;
    return CARA_EOK;
}

static void bt_put(struct CarafsMount *m, struct CarafsBtNode *n)
{
    carafs_cache_put(m, n->block_no);
}

static int bt_dirty(struct CarafsMount *m, struct CarafsBtNode *n)
{
    carafs_put_crc((u8 *)n, m->block_size, offsetof(struct CarafsBtNode, crc32c));
    return carafs_txn_dirty(m, n->block_no);
}

// Allocate + pin + initialise a fresh node. Caller dirties and puts.
static int bt_new(struct CarafsMount *m, u64 hint, u8 level, struct CarafsBtNode **out)
{
    u64 block;
    int rc = carafs_alloc_block(m, hint, &block);
    if (rc != CARA_EOK) {
        return rc;
    }
    u8 *blk;
    rc = carafs_cache_get(m, block, CARAFS_GET_ZERO, &blk, nullptr);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsBtNode *n = (struct CarafsBtNode *)blk;
    n->magic = CARAFS_MAGIC_BTREE;
    n->block_no = block;
    n->level = level;
    n->flavour = CARAFS_BT_EXTENT;
    *out = n;
    return CARA_EOK;
}

// Greatest interior index whose key <= fblk; 0 when fblk precedes
// every key (descend leftmost — only `next` cares about that edge).
static u32 bt_idescend(const struct CarafsBtNode *n, u64 fblk, bool *before_all)
{
    const struct CarafsBtInteriorRec *r =
        (const struct CarafsBtInteriorRec *)((const u8 *)n + CARAFS_BT_BODY_OFF);
    u32 lo = 0;
    u32 hi = n->n_records;
    while (hi - lo > 1) {
        u32 mid = (lo + hi) / 2;
        if (r[mid].key_hi <= fblk) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    if (before_all) {
        *before_all = n->n_records == 0 || r[0].key_hi > fblk;
    }
    return lo;
}

// Greatest leaf index with file_off <= fblk; n_records when none.
static u32 bt_lfloor(const struct CarafsBtNode *n, u64 fblk)
{
    const struct CarafsExtentRec *r =
        (const struct CarafsExtentRec *)((const u8 *)n + CARAFS_BT_BODY_OFF);
    u32 lo = 0;
    u32 hi = n->n_records;
    if (hi == 0 || r[0].file_off > fblk) {
        return n->n_records;
    }
    while (hi - lo > 1) {
        u32 mid = (lo + hi) / 2;
        if (r[mid].file_off <= fblk) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

[[nodiscard]] int carafs_etree_floor(struct CarafsMount *m, u64 root, u64 fblk,
                                     struct CarafsExtentRec *out)
{
    u64 block = root;
    for (u32 depth = 0; depth < BT_MAX_DEPTH; depth++) {
        struct CarafsBtNode *n;
        int rc = bt_get(m, block, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level > 0) {
            bool before_all;
            u32 i = bt_idescend(n, fblk, &before_all);
            if (before_all) {
                bt_put(m, n);
                return CARA_ENOTFOUND; // keys are subtree minima
            }
            block = bt_irecs(n)[i].child;
            bt_put(m, n);
            continue;
        }
        u32 i = bt_lfloor(n, fblk);
        if (i == n->n_records) {
            bt_put(m, n);
            return CARA_ENOTFOUND;
        }
        *out = bt_erecs(n)[i];
        bt_put(m, n);
        return CARA_EOK;
    }
    return CARA_EBADMAGIC; // deeper than any legal tree — cycle
}

[[nodiscard]] int carafs_etree_next(struct CarafsMount *m, u64 root, u64 fblk,
                                    struct CarafsExtentRec *out)
{
    struct BtPathLvl path[BT_MAX_DEPTH];
    u32 depth = 0;
    u64 block = root;
    // Descend along the floor path, remembering indices.
    for (;;) {
        if (depth == BT_MAX_DEPTH) {
            return CARA_EBADMAGIC;
        }
        struct CarafsBtNode *n;
        int rc = bt_get(m, block, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level > 0) {
            u32 i = bt_idescend(n, fblk, nullptr);
            path[depth] = (struct BtPathLvl){ .block = block, .idx = i };
            depth++;
            block = bt_irecs(n)[i].child;
            bt_put(m, n);
            continue;
        }
        // Leaf: first record strictly past fblk.
        const struct CarafsExtentRec *r = bt_erecs(n);
        for (u32 i = 0; i < n->n_records; i++) {
            if (r[i].file_off > fblk) {
                *out = r[i];
                bt_put(m, n);
                return CARA_EOK;
            }
        }
        bt_put(m, n);
        break;
    }
    // Ascend to the first ancestor with a right sibling, then take
    // that subtree's leftmost leaf record.
    while (depth > 0) {
        depth--;
        struct CarafsBtNode *n;
        int rc = bt_get(m, path[depth].block, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (path[depth].idx + 1 >= n->n_records) {
            bt_put(m, n);
            continue;
        }
        u64 child = bt_irecs(n)[path[depth].idx + 1].child;
        bt_put(m, n);
        for (u32 d = 0; d < BT_MAX_DEPTH; d++) {
            rc = bt_get(m, child, &n);
            if (rc != CARA_EOK) {
                return rc;
            }
            if (n->level > 0) {
                child = bt_irecs(n)[0].child;
                bt_put(m, n);
                continue;
            }
            bool ok = n->n_records > 0;
            if (ok) {
                *out = bt_erecs(n)[0];
            }
            bt_put(m, n);
            return ok ? CARA_EOK : CARA_EBADMAGIC;
        }
        return CARA_EBADMAGIC;
    }
    return CARA_ENOTFOUND;
}

// Insert a 24-byte record at `pos` in a node with room. Works for
// both flavours (same record size).
static void node_insert_at(struct CarafsBtNode *n, u32 pos, const void *rec)
{
    u8 *body = (u8 *)n + CARAFS_BT_BODY_OFF;
    constexpr u32 sz = sizeof(struct CarafsExtentRec);
    static_assert(sizeof(struct CarafsExtentRec) == sizeof(struct CarafsBtInteriorRec));
    memmove(body + (pos + 1) * sz, body + pos * sz, ((usize)n->n_records - pos) * sz);
    memcpy(body + pos * sz, rec, sz);
    n->n_records++;
    n->used_bytes += sz;
}

// Split a full node: move the upper half into a fresh right sibling.
// Returns the sibling (pinned) and its smallest key via *sep.
static int node_split(struct CarafsMount *m, struct CarafsBtNode *n, u64 *sep,
                      struct CarafsBtNode **right_out)
{
    struct CarafsBtNode *right;
    int rc = bt_new(m, n->block_no, n->level, &right);
    if (rc != CARA_EOK) {
        return rc;
    }
    constexpr u32 sz = sizeof(struct CarafsExtentRec);
    u32 keep = n->n_records / 2;
    u32 move = n->n_records - keep;
    u8 *src = (u8 *)n + CARAFS_BT_BODY_OFF + keep * sz;
    memcpy((u8 *)right + CARAFS_BT_BODY_OFF, src, (usize)move * sz);
    memset(src, 0, (usize)move * sz);
    right->n_records = (u16)move;
    right->used_bytes = move * sz;
    n->n_records = (u16)keep;
    n->used_bytes = keep * sz;
    *sep = (n->level == 0) ? bt_erecs(right)[0].file_off : bt_irecs(right)[0].key_hi;
    *right_out = right;
    return CARA_EOK;
}

// The smallest key in a node (leaf or interior).
static u64 node_min_key(struct CarafsBtNode *n)
{
    return (n->level == 0) ? bt_erecs(n)[0].file_off : bt_irecs(n)[0].key_hi;
}

[[nodiscard]] int carafs_etree_insert(struct CarafsMount *m, struct CarafsCnode *cn,
                                      const struct CarafsExtentRec *rec)
{
    struct BtPathLvl path[BT_MAX_DEPTH];
    u32 depth = 0;
    u64 block = cn->tree_root;
    struct CarafsBtNode *n;
    int rc;

    // Descend to the target leaf.
    for (;;) {
        if (depth == BT_MAX_DEPTH) {
            return CARA_EBADMAGIC;
        }
        rc = bt_get(m, block, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level == 0) {
            break;
        }
        u32 i = bt_idescend(n, rec->file_off, nullptr);
        path[depth] = (struct BtPathLvl){ .block = block, .idx = i };
        depth++;
        block = bt_irecs(n)[i].child;
        bt_put(m, n);
    }

    // Leaf insert: predecessor merge first, else place (splitting on
    // overflow). `n` is the pinned leaf.
    u32 pos = bt_lfloor(n, rec->file_off);
    if (pos != n->n_records) {
        struct CarafsExtentRec *pred = &bt_erecs(n)[pos];
        if (pred->file_off + pred->count == rec->file_off &&
            pred->start + pred->count == rec->start && pred->flags == rec->flags &&
            (u64)pred->count + rec->count <= 0xFFFFFFFFull) {
            pred->count += rec->count;
            rc = bt_dirty(m, n);
            bt_put(m, n);
            return rc;
        }
        pos++; // insert after the floor record
    } else {
        pos = 0; // smaller than everything in this leaf
    }

    // Carry an (separator, new right child) pair up the path while
    // nodes overflow.
    bool carry = false;
    u64 carry_key = 0;
    u64 carry_child = 0;
    if (n->n_records < bt_cap(m)) {
        node_insert_at(n, pos, rec);
        rc = bt_dirty(m, n);
        bt_put(m, n);
        if (rc != CARA_EOK) {
            return rc;
        }
    } else {
        u64 sep;
        struct CarafsBtNode *right;
        rc = node_split(m, n, &sep, &right);
        if (rc != CARA_EOK) {
            bt_put(m, n);
            return rc;
        }
        if (rec->file_off >= sep) {
            node_insert_at(right, pos - n->n_records, rec);
        } else {
            node_insert_at(n, pos, rec);
        }
        carry = true;
        carry_key = node_min_key(right);
        carry_child = right->block_no;
        rc = bt_dirty(m, n);
        int rc2 = bt_dirty(m, right);
        bt_put(m, n);
        bt_put(m, right);
        if (rc != CARA_EOK || rc2 != CARA_EOK) {
            return rc != CARA_EOK ? rc : rc2;
        }
    }

    u32 lvl = depth;
    while (carry && lvl > 0) {
        lvl--;
        rc = bt_get(m, path[lvl].block, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        struct CarafsBtInteriorRec irec = { .key_hi = carry_key,
                                            .key_lo = 0,
                                            .child = carry_child };
        u32 ipos = path[lvl].idx + 1;
        if (n->n_records < bt_cap(m)) {
            node_insert_at(n, ipos, &irec);
            carry = false;
            rc = bt_dirty(m, n);
            bt_put(m, n);
            if (rc != CARA_EOK) {
                return rc;
            }
        } else {
            u64 sep;
            struct CarafsBtNode *right;
            rc = node_split(m, n, &sep, &right);
            if (rc != CARA_EOK) {
                bt_put(m, n);
                return rc;
            }
            if (irec.key_hi >= sep) {
                node_insert_at(right, ipos - n->n_records, &irec);
            } else {
                node_insert_at(n, ipos, &irec);
            }
            carry_key = node_min_key(right);
            carry_child = right->block_no;
            rc = bt_dirty(m, n);
            int rc2 = bt_dirty(m, right);
            bt_put(m, n);
            bt_put(m, right);
            if (rc != CARA_EOK || rc2 != CARA_EOK) {
                return rc != CARA_EOK ? rc : rc2;
            }
        }
    }
    if (carry) {
        // Root split: the tree grows a level.
        struct CarafsBtNode *old_root;
        rc = bt_get(m, cn->tree_root, &old_root);
        if (rc != CARA_EOK) {
            return rc;
        }
        u64 left_key = node_min_key(old_root);
        u8 new_level = old_root->level + 1;
        bt_put(m, old_root);
        struct CarafsBtNode *root;
        rc = bt_new(m, cn->block_no, new_level, &root);
        if (rc != CARA_EOK) {
            return rc;
        }
        bt_irecs(root)[0] = (struct CarafsBtInteriorRec){ .key_hi = left_key,
                                                          .child = cn->tree_root };
        bt_irecs(root)[1] = (struct CarafsBtInteriorRec){ .key_hi = carry_key,
                                                          .child = carry_child };
        root->n_records = 2;
        root->used_bytes = 2 * sizeof(struct CarafsBtInteriorRec);
        rc = bt_dirty(m, root);
        cn->tree_root = root->block_no; // caller re-crcs the cnode
        bt_put(m, root);
        if (rc != CARA_EOK) {
            return rc;
        }
    }

    // Separator fix-up: an insert at leaf position 0 lowers subtree
    // minima; walk the recorded path and repair any stale keys.
    for (u32 d = depth; d-- > 0;) {
        struct CarafsBtNode *child;
        u64 child_block = (d == depth - 1) ? block : path[d + 1].block;
        rc = bt_get(m, child_block, &child);
        if (rc != CARA_EOK) {
            return rc;
        }
        u64 min_key = node_min_key(child);
        bt_put(m, child);
        struct CarafsBtNode *parent;
        rc = bt_get(m, path[d].block, &parent);
        if (rc != CARA_EOK) {
            return rc;
        }
        struct CarafsBtInteriorRec *pr = &bt_irecs(parent)[path[d].idx];
        if (pr->key_hi != min_key) {
            pr->key_hi = min_key;
            rc = bt_dirty(m, parent);
        }
        bt_put(m, parent);
        if (rc != CARA_EOK) {
            return rc;
        }
    }
    return CARA_EOK;
}

[[nodiscard]] int carafs_etree_spill(struct CarafsMount *m, struct CarafsCnode *cn)
{
    struct CarafsBtNode *leaf;
    int rc = bt_new(m, cn->block_no, 0, &leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsExtentRec *r = bt_erecs(leaf);
    u64 fo = 0;
    u32 n = 0;
    for (u32 i = 0; i < cn->n_inline_extents; i++) {
        if (cn->ext[i].start != 0) { // holes become absent keys
            r[n++] = (struct CarafsExtentRec){
                .file_off = fo,
                .start = cn->ext[i].start,
                .count = cn->ext[i].count,
                .flags = cn->ext[i].flags,
            };
        }
        fo += cn->ext[i].count;
    }
    leaf->n_records = (u16)n;
    leaf->used_bytes = n * sizeof(struct CarafsExtentRec);
    rc = bt_dirty(m, leaf);
    u64 leaf_block = leaf->block_no;
    bt_put(m, leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    cn->tree_root = leaf_block;
    cn->n_inline_extents = 0;
    memset(cn->ext, 0, sizeof(cn->ext));
    return CARA_EOK;
}

[[nodiscard]] int carafs_etree_free_all(struct CarafsMount *m, struct CarafsCnode *cn)
{
    struct {
        u64 block;
        u32 next_child;
    } st[BT_MAX_DEPTH];
    u32 sp = 0;
    st[sp++] = (typeof(st[0])){ .block = cn->tree_root };

    while (sp > 0) {
        struct CarafsBtNode *n;
        int rc = bt_get(m, st[sp - 1].block, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level == 0) {
            const struct CarafsExtentRec *r = bt_erecs(n);
            u16 cnt = n->n_records;
            for (u16 i = 0; i < cnt; i++) {
                rc = carafs_free_extent(m, r[i].start, r[i].count);
                if (rc != CARA_EOK) {
                    bt_put(m, n);
                    return rc;
                }
            }
            bt_put(m, n);
            rc = carafs_free_extent(m, st[sp - 1].block, 1);
            if (rc != CARA_EOK) {
                return rc;
            }
            sp--;
            continue;
        }
        if (st[sp - 1].next_child < n->n_records) {
            u64 child = bt_irecs(n)[st[sp - 1].next_child].child;
            st[sp - 1].next_child++;
            bt_put(m, n);
            if (sp == BT_MAX_DEPTH) {
                return CARA_EBADMAGIC;
            }
            st[sp++] = (typeof(st[0])){ .block = child };
            continue;
        }
        bt_put(m, n);
        int rc2 = carafs_free_extent(m, st[sp - 1].block, 1);
        if (rc2 != CARA_EOK) {
            return rc2;
        }
        sp--;
    }
    cn->tree_root = 0;
    return CARA_EOK;
}
