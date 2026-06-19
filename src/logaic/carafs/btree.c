// SPDX-License-Identifier: BSD-2-Clause
//
// Shared B+tree (CARAFS.md §3.6/§3.7). One node format, two flavours:
//
//   EXTENT (F2) — fixed 24-byte CarafsExtentRec leaves keyed by file
//     block offset; interior records are CarafsBtInteriorRec. Holes are
//     absent keys; inserts merge with a physically-adjacent predecessor.
//   DIR (F3) — variable-stride CarafsDirent leaves keyed by the
//     composite (name_hash u64, collision_seq u8); interior records are
//     CarafsBtInteriorRec carrying that key in (key_hi, key_lo).
//
// Keys are composite (hi, lo) throughout; the extent flavour pins
// lo = 0, so its tree is the degenerate single-u64 case. The interior
// machinery — descent, the integrated upward propagation that fixes
// subtree-minimum separators and threads split-carries to the root,
// and root growth — is shared; only the leaf record shape and the
// merge/compare rules are flavour-specific.
//
// Pin discipline: at most a couple of tree nodes are pinned at a time;
// walks remember (block, index) per level and re-get ancestors (cache
// hits) instead of holding pins down the path. Mutators run inside the
// caller's open transaction; node blocks come from / return to the AG
// allocator.

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

// 169 interior records per 4 KiB node → depth 4 maps > 10^9 entries; 8
// is unreachable headroom (and holds for 512 B nodes: 20^8 >> 2^32).
constexpr u32 BT_MAX_DEPTH = 8;

struct BtPathLvl {
    u64 block;
    u32 idx; // descent index taken at this (interior) level
};

// ---- Generic node helpers -----------------------------------------------------

static u32 bt_cap(const struct CarafsMount *m)
{
    return (m->block_size - CARAFS_BT_BODY_OFF) / sizeof(struct CarafsBtInteriorRec);
}

static u8 *bt_body(struct CarafsBtNode *n)
{
    return (u8 *)n + CARAFS_BT_BODY_OFF;
}

static struct CarafsBtInteriorRec *bt_irecs(struct CarafsBtNode *n)
{
    return (struct CarafsBtInteriorRec *)((u8 *)n + CARAFS_BT_BODY_OFF);
}

static struct CarafsExtentRec *bt_erecs(struct CarafsBtNode *n)
{
    return (struct CarafsExtentRec *)((u8 *)n + CARAFS_BT_BODY_OFF);
}

static int key_cmp(u64 ahi, u64 alo, u64 bhi, u64 blo)
{
    if (ahi != bhi) {
        return ahi < bhi ? -1 : 1;
    }
    if (alo != blo) {
        return alo < blo ? -1 : 1;
    }
    return 0;
}

static u32 dirent_stride(u32 name_len)
{
    return Carafs_Align8(CARAFS_DIRENT_BASE + name_len);
}

// Smallest key in a (non-empty) node, as a composite (hi, lo).
static void node_min_key(const struct CarafsBtNode *n, u64 *hi, u64 *lo)
{
    if (n->level > 0) {
        const struct CarafsBtInteriorRec *r =
            (const struct CarafsBtInteriorRec *)((const u8 *)n + CARAFS_BT_BODY_OFF);
        *hi = r[0].key_hi;
        *lo = r[0].key_lo;
    } else if (n->flavour == CARAFS_BT_EXTENT) {
        const struct CarafsExtentRec *r =
            (const struct CarafsExtentRec *)((const u8 *)n + CARAFS_BT_BODY_OFF);
        *hi = r[0].file_off;
        *lo = 0;
    } else { // DIR leaf
        const struct CarafsDirent *d =
            (const struct CarafsDirent *)((const u8 *)n + CARAFS_BT_BODY_OFF);
        *hi = d->hash;
        *lo = d->seq;
    }
}

static int bt_get(struct CarafsMount *m, u64 block, u8 flavour, struct CarafsBtNode **out)
{
    bool fresh;
    u8 *blk;
    int rc = carafs_cache_get(m, block, CARAFS_GET_READ, &blk, &fresh);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsBtNode *n = (struct CarafsBtNode *)blk;
    if (n->magic != CARAFS_MAGIC_BTREE || n->block_no != block || n->flavour != flavour ||
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
static int bt_new(struct CarafsMount *m, u64 hint, u8 level, u8 flavour, struct CarafsBtNode **out)
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
    n->flavour = flavour;
    *out = n;
    return CARA_EOK;
}

// Greatest interior index whose key <= (hi, lo); 0 when the key precedes
// every record (descend leftmost — only forward iteration cares).
static u32 bt_idescend(const struct CarafsBtNode *n, u64 hi, u64 lo, bool *before_all)
{
    const struct CarafsBtInteriorRec *r =
        (const struct CarafsBtInteriorRec *)((const u8 *)n + CARAFS_BT_BODY_OFF);
    u32 loi = 0;
    u32 hii = n->n_records;
    while (hii - loi > 1) {
        u32 mid = (loi + hii) / 2;
        if (key_cmp(r[mid].key_hi, r[mid].key_lo, hi, lo) <= 0) {
            loi = mid;
        } else {
            hii = mid;
        }
    }
    if (before_all) {
        *before_all = n->n_records == 0 || key_cmp(r[0].key_hi, r[0].key_lo, hi, lo) > 0;
    }
    return loi;
}

// Insert a fixed 24-byte record at interior/extent-leaf position `pos`.
static void node_insert_at(struct CarafsBtNode *n, u32 pos, const void *rec)
{
    u8 *body = (u8 *)n + CARAFS_BT_BODY_OFF;
    constexpr u32 sz = sizeof(struct CarafsBtInteriorRec);
    static_assert(sizeof(struct CarafsExtentRec) == sizeof(struct CarafsBtInteriorRec));
    memmove(body + (pos + 1) * sz, body + pos * sz, ((usize)n->n_records - pos) * sz);
    memcpy(body + pos * sz, rec, sz);
    n->n_records++;
    n->used_bytes += sz;
}

// Split a full fixed-record node (interior or extent leaf): move the
// upper half into a fresh right sibling. Returns the sibling (pinned)
// and its smallest key via *sep_*.
static int node_split_fixed(struct CarafsMount *m, struct CarafsBtNode *n, u8 flavour, u64 *sep_hi,
                            u64 *sep_lo, struct CarafsBtNode **right_out)
{
    struct CarafsBtNode *right;
    int rc = bt_new(m, n->block_no, n->level, flavour, &right);
    if (rc != CARA_EOK) {
        return rc;
    }
    constexpr u32 sz = sizeof(struct CarafsBtInteriorRec);
    u32 keep = n->n_records / 2;
    u32 move = n->n_records - keep;
    u8 *src = (u8 *)n + CARAFS_BT_BODY_OFF + keep * sz;
    memcpy((u8 *)right + CARAFS_BT_BODY_OFF, src, (usize)move * sz);
    memset(src, 0, (usize)move * sz);
    right->n_records = (u16)move;
    right->used_bytes = move * sz;
    n->n_records = (u16)keep;
    n->used_bytes = keep * sz;
    node_min_key(right, sep_hi, sep_lo);
    *right_out = right;
    return CARA_EOK;
}

// After a leaf at the bottom of `path` was modified — and optionally a
// right sibling `extra_child` (with min key extra_*) was split off —
// walk the path to the root: at each ancestor fix the separator for the
// child we ascended from to that child's current minimum (repairs the
// stale-high key an insert-at-position-0 leaves behind) and thread the
// pending split-carry upward, splitting interiors and growing the root
// as needed. May update cn->tree_root.
static int bt_propagate(struct CarafsMount *m, struct CarafsCnode *cn, u8 flavour,
                        const struct BtPathLvl *path, u32 depth, u64 leaf_block, bool carry,
                        u64 carry_hi, u64 carry_lo, u64 carry_child)
{
    u64 child_block = leaf_block;
    for (u32 d = depth; d-- > 0;) {
        struct CarafsBtNode *parent;
        int rc = bt_get(m, path[d].block, flavour, &parent);
        if (rc != CARA_EOK) {
            return rc;
        }
        bool dirtied = false;

        // 1. Fix the separator for the child we came up from.
        struct CarafsBtNode *child;
        rc = bt_get(m, child_block, flavour, &child);
        if (rc != CARA_EOK) {
            bt_put(m, parent);
            return rc;
        }
        u64 chi, clo;
        node_min_key(child, &chi, &clo);
        bt_put(m, child);
        struct CarafsBtInteriorRec *pr = &bt_irecs(parent)[path[d].idx];
        if (pr->key_hi != chi || pr->key_lo != clo) {
            pr->key_hi = chi;
            pr->key_lo = clo;
            dirtied = true;
        }

        // 2. Insert the pending carry just after that child slot.
        if (carry) {
            struct CarafsBtInteriorRec irec = { .key_hi = carry_hi,
                                                .key_lo = carry_lo,
                                                .child = carry_child };
            u32 ipos = path[d].idx + 1;
            if (parent->n_records < bt_cap(m)) {
                node_insert_at(parent, ipos, &irec);
                carry = false;
                dirtied = true;
            } else {
                u64 sep_hi, sep_lo;
                struct CarafsBtNode *right;
                rc = node_split_fixed(m, parent, flavour, &sep_hi, &sep_lo, &right);
                if (rc != CARA_EOK) {
                    bt_put(m, parent);
                    return rc;
                }
                if (key_cmp(irec.key_hi, irec.key_lo, sep_hi, sep_lo) >= 0) {
                    node_insert_at(right, ipos - parent->n_records, &irec);
                } else {
                    node_insert_at(parent, ipos, &irec);
                }
                node_min_key(right, &carry_hi, &carry_lo);
                carry_child = right->block_no;
                int rcr = bt_dirty(m, right);
                bt_put(m, right);
                if (rcr != CARA_EOK) {
                    bt_put(m, parent);
                    return rcr;
                }
                dirtied = true;
            }
        }

        if (dirtied) {
            rc = bt_dirty(m, parent);
            if (rc != CARA_EOK) {
                bt_put(m, parent);
                return rc;
            }
        }
        child_block = path[d].block;
        bt_put(m, parent);
    }

    if (carry) {
        // Root split: the tree grows a level.
        struct CarafsBtNode *old_root;
        int rc = bt_get(m, cn->tree_root, flavour, &old_root);
        if (rc != CARA_EOK) {
            return rc;
        }
        u64 lhi, llo;
        node_min_key(old_root, &lhi, &llo);
        u8 new_level = old_root->level + 1;
        bt_put(m, old_root);
        struct CarafsBtNode *root;
        rc = bt_new(m, cn->block_no, new_level, flavour, &root);
        if (rc != CARA_EOK) {
            return rc;
        }
        bt_irecs(root)[0] =
            (struct CarafsBtInteriorRec){ .key_hi = lhi, .key_lo = llo, .child = cn->tree_root };
        bt_irecs(root)[1] = (struct CarafsBtInteriorRec){ .key_hi = carry_hi,
                                                          .key_lo = carry_lo,
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
    return CARA_EOK;
}

// ================= Extent flavour (F2) =========================================

// Greatest extent-leaf index with file_off <= fblk; n_records when none.
static u32 et_lfloor(const struct CarafsBtNode *n, u64 fblk)
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
        int rc = bt_get(m, block, CARAFS_BT_EXTENT, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level > 0) {
            bool before_all;
            u32 i = bt_idescend(n, fblk, 0, &before_all);
            if (before_all) {
                bt_put(m, n);
                return CARA_ENOTFOUND; // keys are subtree minima
            }
            block = bt_irecs(n)[i].child;
            bt_put(m, n);
            continue;
        }
        u32 i = et_lfloor(n, fblk);
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
        int rc = bt_get(m, block, CARAFS_BT_EXTENT, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level > 0) {
            u32 i = bt_idescend(n, fblk, 0, nullptr);
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
    // Ascend to the first ancestor with a right sibling, then take that
    // subtree's leftmost leaf record.
    while (depth > 0) {
        depth--;
        struct CarafsBtNode *n;
        int rc = bt_get(m, path[depth].block, CARAFS_BT_EXTENT, &n);
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
            rc = bt_get(m, child, CARAFS_BT_EXTENT, &n);
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
        rc = bt_get(m, block, CARAFS_BT_EXTENT, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level == 0) {
            break;
        }
        u32 i = bt_idescend(n, rec->file_off, 0, nullptr);
        path[depth] = (struct BtPathLvl){ .block = block, .idx = i };
        depth++;
        block = bt_irecs(n)[i].child;
        bt_put(m, n);
    }

    // Leaf insert: predecessor merge first, else place (splitting on
    // overflow). `n` is the pinned leaf.
    u32 pos = et_lfloor(n, rec->file_off);
    if (pos != n->n_records) {
        struct CarafsExtentRec *pred = &bt_erecs(n)[pos];
        if (pred->file_off + pred->count == rec->file_off &&
            pred->start + pred->count == rec->start && pred->flags == rec->flags &&
            (u64)pred->count + rec->count <= 0xFFFFFFFFull) {
            pred->count += rec->count;
            rc = bt_dirty(m, n);
            bt_put(m, n);
            return rc; // key unchanged → no separator repair needed
        }
        pos++; // insert after the floor record
    } else {
        pos = 0; // smaller than everything in this leaf
    }

    if (n->n_records < bt_cap(m)) {
        node_insert_at(n, pos, rec);
        rc = bt_dirty(m, n);
        bt_put(m, n);
        if (rc != CARA_EOK) {
            return rc;
        }
        return bt_propagate(m, cn, CARAFS_BT_EXTENT, path, depth, block, false, 0, 0, 0);
    }

    // Leaf full: split, insert into the right half, carry up.
    u64 sep_hi, sep_lo;
    struct CarafsBtNode *right;
    rc = node_split_fixed(m, n, CARAFS_BT_EXTENT, &sep_hi, &sep_lo, &right);
    if (rc != CARA_EOK) {
        bt_put(m, n);
        return rc;
    }
    if (key_cmp(rec->file_off, 0, sep_hi, sep_lo) >= 0) {
        node_insert_at(right, pos - n->n_records, rec);
    } else {
        node_insert_at(n, pos, rec);
    }
    u64 carry_hi, carry_lo;
    node_min_key(right, &carry_hi, &carry_lo);
    u64 carry_child = right->block_no;
    rc = bt_dirty(m, n);
    int rc2 = bt_dirty(m, right);
    bt_put(m, n);
    bt_put(m, right);
    if (rc != CARA_EOK || rc2 != CARA_EOK) {
        return rc != CARA_EOK ? rc : rc2;
    }
    return bt_propagate(m, cn, CARAFS_BT_EXTENT, path, depth, block, true, carry_hi, carry_lo,
                        carry_child);
}

[[nodiscard]] int carafs_etree_spill(struct CarafsMount *m, struct CarafsCnode *cn)
{
    struct CarafsBtNode *leaf;
    int rc = bt_new(m, cn->block_no, 0, CARAFS_BT_EXTENT, &leaf);
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

// Post-order free of a tree's node blocks. `free_extents` additionally
// returns every extent an EXTENT-flavour leaf maps. Clears tree_root.
static int tree_free_all(struct CarafsMount *m, struct CarafsCnode *cn, u8 flavour,
                         bool free_extents)
{
    struct {
        u64 block;
        u32 next_child;
    } st[BT_MAX_DEPTH];
    u32 sp = 0;
    st[sp++] = (typeof(st[0])){ .block = cn->tree_root };

    while (sp > 0) {
        struct CarafsBtNode *n;
        int rc = bt_get(m, st[sp - 1].block, flavour, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level == 0) {
            if (free_extents) {
                const struct CarafsExtentRec *r = bt_erecs(n);
                u16 cnt = n->n_records;
                for (u16 i = 0; i < cnt; i++) {
                    rc = carafs_free_extent(m, r[i].start, r[i].count);
                    if (rc != CARA_EOK) {
                        bt_put(m, n);
                        return rc;
                    }
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

[[nodiscard]] int carafs_etree_free_all(struct CarafsMount *m, struct CarafsCnode *cn)
{
    return tree_free_all(m, cn, CARAFS_BT_EXTENT, true);
}

// ================= Directory flavour (F3) =====================================

// Byte offset of dirent index `idx` (0..n_records) in a DIR leaf body.
static u32 dleaf_off(const u8 *body, u32 idx)
{
    u32 o = 0;
    for (u32 i = 0; i < idx; i++) {
        const struct CarafsDirent *d = (const struct CarafsDirent *)(body + o);
        o += dirent_stride(d->name_len);
    }
    return o;
}

// Descend to the DIR leaf that would hold key (hash, seq), recording the
// interior path. Returns the pinned leaf via *leaf.
static int dtree_descend(struct CarafsMount *m, u64 root, u64 hash, u8 seq, struct BtPathLvl *path,
                         u32 *depth_out, struct CarafsBtNode **leaf)
{
    u32 depth = 0;
    u64 block = root;
    for (;;) {
        if (depth == BT_MAX_DEPTH) {
            return CARA_EBADMAGIC;
        }
        struct CarafsBtNode *n;
        int rc = bt_get(m, block, CARAFS_BT_DIR, &n);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (n->level == 0) {
            *leaf = n;
            *depth_out = depth;
            return CARA_EOK;
        }
        u32 i = bt_idescend(n, hash, seq, nullptr);
        path[depth] = (struct BtPathLvl){ .block = block, .idx = i };
        depth++;
        block = bt_irecs(n)[i].child;
        bt_put(m, n);
    }
}

// Block number of the leaf immediately to the right of the leaf reached
// by `path`, or CARAFS_BLOCK_NONE at the end. Leaves no net pins.
static int dtree_next_leaf(struct CarafsMount *m, const struct BtPathLvl *path, u32 depth,
                           u64 *next_out)
{
    *next_out = CARAFS_BLOCK_NONE;
    while (depth > 0) {
        depth--;
        struct CarafsBtNode *n;
        int rc = bt_get(m, path[depth].block, CARAFS_BT_DIR, &n);
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
            rc = bt_get(m, child, CARAFS_BT_DIR, &n);
            if (rc != CARA_EOK) {
                return rc;
            }
            if (n->level > 0) {
                child = bt_irecs(n)[0].child;
                bt_put(m, n);
                continue;
            }
            bt_put(m, n);
            *next_out = child;
            return CARA_EOK;
        }
        return CARA_EBADMAGIC;
    }
    return CARA_EOK; // no right sibling
}

// Scan the equal-hash run for `name`: report whether it already exists
// (and copy the matching dirent header to *match) and the smallest
// unused collision_seq among equal-hash entries (for a new insert).
[[nodiscard]] int carafs_dtree_scan(struct CarafsMount *m, u64 root, u64 hash, const void *name,
                                    u32 name_len, bool *exists, struct CarafsDirent *match,
                                    u8 *free_seq)
{
    u8 used[32];
    memset(used, 0, sizeof(used));
    bool found = false;
    u8 s = 0;
    bool more = true;
    while (more) {
        struct BtPathLvl path[BT_MAX_DEPTH];
        u32 depth;
        struct CarafsBtNode *leaf;
        int rc = dtree_descend(m, root, hash, s, path, &depth, &leaf);
        if (rc != CARA_EOK) {
            return rc;
        }
        more = false;
        u8 *body = bt_body(leaf);
        u32 off = 0;
        u32 cnt = leaf->n_records;
        for (u32 i = 0; i < cnt; i++) {
            struct CarafsDirent *d = (struct CarafsDirent *)(body + off);
            if (d->hash == hash && d->seq >= s) {
                used[d->seq >> 3] |= (u8)(1u << (d->seq & 7));
                if (!found &&
                    Carafs_NameEq(body + off + CARAFS_DIRENT_BASE, d->name_len, name, name_len)) {
                    found = true;
                    if (match) {
                        *match = *d;
                    }
                }
                // Advance the run cursor; the run may continue into the
                // next leaf only if this is the last record and seq has
                // room left (no u8 wrap).
                u8 next = (u8)(d->seq + 1);
                if (i == cnt - 1 && next != 0) {
                    s = next;
                    more = true;
                }
            } else if (d->hash > hash) {
                more = false;
                break;
            }
            off += dirent_stride(d->name_len);
        }
        bt_put(m, leaf);
    }
    *exists = found;
    if (free_seq) {
        u32 fs = 0;
        while (fs < 256 && (used[fs >> 3] & (1u << (fs & 7)))) {
            fs++;
        }
        if (fs >= 256) {
            return CARA_EOVERFLOW; // 256 colliding names on one hash (absurd)
        }
        *free_seq = (u8)fs;
    }
    return CARA_EOK;
}

[[nodiscard]] int carafs_dtree_lookup(struct CarafsMount *m, u64 root, u64 hash, const void *name,
                                      u32 name_len, struct CarafsDirent *out)
{
    bool exists;
    int rc = carafs_dtree_scan(m, root, hash, name, name_len, &exists, out, nullptr);
    if (rc != CARA_EOK) {
        return rc;
    }
    return exists ? CARA_EOK : CARA_ENOTFOUND;
}

// First entry with key strictly greater than (hash, seq) when `strict`,
// or greater-or-equal otherwise. Copies the header to *out and the name
// bytes to name_out (>= 255 bytes). CARA_ENOTFOUND at the end.
[[nodiscard]] int carafs_dtree_next(struct CarafsMount *m, u64 root, u64 hash, u8 seq, bool strict,
                                    struct CarafsDirent *out, u8 *name_out)
{
    struct BtPathLvl path[BT_MAX_DEPTH];
    u32 depth;
    struct CarafsBtNode *leaf;
    int rc = dtree_descend(m, root, hash, seq, path, &depth, &leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    u8 *body = bt_body(leaf);
    u32 off = 0;
    for (u32 i = 0; i < leaf->n_records; i++) {
        struct CarafsDirent *d = (struct CarafsDirent *)(body + off);
        int c = key_cmp(d->hash, d->seq, hash, seq);
        if (strict ? c > 0 : c >= 0) {
            *out = *d;
            if (name_out) {
                memcpy(name_out, body + off + CARAFS_DIRENT_BASE, d->name_len);
            }
            bt_put(m, leaf);
            return CARA_EOK;
        }
        off += dirent_stride(d->name_len);
    }
    // Not in this leaf — the answer is the first record of the next leaf.
    u64 nb;
    rc = dtree_next_leaf(m, path, depth, &nb);
    bt_put(m, leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (nb == CARAFS_BLOCK_NONE) {
        return CARA_ENOTFOUND;
    }
    struct CarafsBtNode *nl;
    rc = bt_get(m, nb, CARAFS_BT_DIR, &nl);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (nl->n_records == 0) {
        bt_put(m, nl);
        return CARA_ENOTFOUND;
    }
    struct CarafsDirent *d = (struct CarafsDirent *)bt_body(nl);
    *out = *d;
    if (name_out) {
        memcpy(name_out, bt_body(nl) + CARAFS_DIRENT_BASE, d->name_len);
    }
    bt_put(m, nl);
    return CARA_EOK;
}

// Insert a fully-formed dirent (caller guarantees the key (hash, seq) is
// unique). `name` holds de->name_len bytes.
[[nodiscard]] int carafs_dtree_insert(struct CarafsMount *m, struct CarafsCnode *cn,
                                      const struct CarafsDirent *de, const void *name)
{
    struct BtPathLvl path[BT_MAX_DEPTH];
    u32 depth;
    struct CarafsBtNode *leaf;
    int rc = dtree_descend(m, cn->tree_root, de->hash, de->seq, path, &depth, &leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    u32 stride = dirent_stride(de->name_len);
    u32 cap = m->block_size - CARAFS_BT_BODY_OFF;
    u8 *body = bt_body(leaf);

    // Sorted insertion offset within this leaf.
    u32 off = 0;
    for (u32 i = 0; i < leaf->n_records; i++) {
        struct CarafsDirent *d = (struct CarafsDirent *)(body + off);
        if (key_cmp(d->hash, d->seq, de->hash, de->seq) > 0) {
            break;
        }
        off += dirent_stride(d->name_len);
    }

    if (leaf->used_bytes + stride <= cap) {
        memmove(body + off + stride, body + off, leaf->used_bytes - off);
        struct CarafsDirent *nd = (struct CarafsDirent *)(body + off);
        *nd = *de;
        memcpy(body + off + CARAFS_DIRENT_BASE, name, de->name_len);
        memset(body + off + CARAFS_DIRENT_BASE + de->name_len, 0,
               stride - CARAFS_DIRENT_BASE - de->name_len);
        leaf->n_records++;
        leaf->used_bytes += stride;
        u64 leaf_block = leaf->block_no;
        rc = bt_dirty(m, leaf);
        bt_put(m, leaf);
        if (rc != CARA_EOK) {
            return rc;
        }
        return bt_propagate(m, cn, CARAFS_BT_DIR, path, depth, leaf_block, false, 0, 0, 0);
    }

    // Leaf full: split near the byte midpoint (both halves non-empty),
    // then insert into the correct half and carry up.
    if (leaf->n_records < 2) {
        bt_put(m, leaf);
        return CARA_EOVERFLOW; // a single entry larger than half a block
    }
    struct CarafsBtNode *right;
    rc = bt_new(m, leaf->block_no, 0, CARAFS_BT_DIR, &right);
    if (rc != CARA_EOK) {
        bt_put(m, leaf);
        return rc;
    }
    u32 keep = 1;
    {
        u32 acc = 0;
        u32 o = 0;
        for (u32 i = 0; i < leaf->n_records; i++) {
            const struct CarafsDirent *d = (const struct CarafsDirent *)(body + o);
            acc += dirent_stride(d->name_len);
            o += dirent_stride(d->name_len);
            if (acc * 2 >= leaf->used_bytes) {
                keep = i + 1;
                break;
            }
        }
        if (keep < 1) {
            keep = 1;
        }
        if (keep > leaf->n_records - 1u) {
            keep = leaf->n_records - 1u;
        }
    }
    u32 split_off = dleaf_off(body, keep);
    u32 move_bytes = leaf->used_bytes - split_off;
    u32 move_cnt = leaf->n_records - keep;
    memcpy(bt_body(right), body + split_off, move_bytes);
    memset(body + split_off, 0, move_bytes);
    right->n_records = (u16)move_cnt;
    right->used_bytes = move_bytes;
    leaf->n_records = (u16)keep;
    leaf->used_bytes = split_off;

    // Place the new record in whichever half owns its key.
    u64 rmin_hi, rmin_lo;
    node_min_key(right, &rmin_hi, &rmin_lo);
    struct CarafsBtNode *tgt = key_cmp(de->hash, de->seq, rmin_hi, rmin_lo) >= 0 ? right : leaf;
    u8 *tb = bt_body(tgt);
    u32 toff = 0;
    for (u32 i = 0; i < tgt->n_records; i++) {
        struct CarafsDirent *d = (struct CarafsDirent *)(tb + toff);
        if (key_cmp(d->hash, d->seq, de->hash, de->seq) > 0) {
            break;
        }
        toff += dirent_stride(d->name_len);
    }
    memmove(tb + toff + stride, tb + toff, tgt->used_bytes - toff);
    struct CarafsDirent *nd = (struct CarafsDirent *)(tb + toff);
    *nd = *de;
    memcpy(tb + toff + CARAFS_DIRENT_BASE, name, de->name_len);
    memset(tb + toff + CARAFS_DIRENT_BASE + de->name_len, 0,
           stride - CARAFS_DIRENT_BASE - de->name_len);
    tgt->n_records++;
    tgt->used_bytes += stride;

    u64 carry_hi, carry_lo;
    node_min_key(right, &carry_hi, &carry_lo);
    u64 carry_child = right->block_no;
    u64 leaf_block = leaf->block_no;
    rc = bt_dirty(m, leaf);
    int rc2 = bt_dirty(m, right);
    bt_put(m, leaf);
    bt_put(m, right);
    if (rc != CARA_EOK || rc2 != CARA_EOK) {
        return rc != CARA_EOK ? rc : rc2;
    }
    return bt_propagate(m, cn, CARAFS_BT_DIR, path, depth, leaf_block, true, carry_hi, carry_lo,
                        carry_child);
}

// Remove the interior record at path[level].idx; if the node empties,
// free it and recurse upward. On the way down, separators above a
// surviving node are repaired (its minimum may have risen).
static int dtree_collapse(struct CarafsMount *m, struct CarafsCnode *cn,
                          const struct BtPathLvl *path, u32 level)
{
    for (;;) {
        struct CarafsBtNode *node;
        int rc = bt_get(m, path[level].block, CARAFS_BT_DIR, &node);
        if (rc != CARA_EOK) {
            return rc;
        }
        struct CarafsBtInteriorRec *r = bt_irecs(node);
        u32 idx = path[level].idx;
        memmove(&r[idx], &r[idx + 1], ((usize)node->n_records - idx - 1) * sizeof(*r));
        node->n_records--;
        node->used_bytes -= sizeof(struct CarafsBtInteriorRec);
        u64 nb = node->block_no;
        if (node->n_records == 0) {
            bt_put(m, node);
            rc = carafs_free_extent(m, nb, 1);
            if (rc != CARA_EOK) {
                return rc;
            }
            if (level == 0) {
                cn->tree_root = 0; // whole tree gone
                return CARA_EOK;
            }
            level--;
            continue;
        }
        rc = bt_dirty(m, node);
        bt_put(m, node);
        if (rc != CARA_EOK) {
            return rc;
        }
        // Surviving node: repair separators above it (removing idx 0
        // raises its minimum).
        if (level > 0) {
            return bt_propagate(m, cn, CARAFS_BT_DIR, path, level, path[level].block, false, 0, 0,
                                0);
        }
        return CARA_EOK;
    }
}

[[nodiscard]] int carafs_dtree_remove(struct CarafsMount *m, struct CarafsCnode *cn, u64 hash,
                                      u8 seq)
{
    struct BtPathLvl path[BT_MAX_DEPTH];
    u32 depth;
    struct CarafsBtNode *leaf;
    int rc = dtree_descend(m, cn->tree_root, hash, seq, path, &depth, &leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    u8 *body = bt_body(leaf);
    u32 off = 0;
    bool found = false;
    for (u32 i = 0; i < leaf->n_records; i++) {
        struct CarafsDirent *d = (struct CarafsDirent *)(body + off);
        if (d->hash == hash && d->seq == seq) {
            found = true;
            break;
        }
        if (key_cmp(d->hash, d->seq, hash, seq) > 0) {
            break;
        }
        off += dirent_stride(d->name_len);
    }
    if (!found) {
        bt_put(m, leaf);
        return CARA_ENOTFOUND;
    }
    u32 stride = dirent_stride(((struct CarafsDirent *)(body + off))->name_len);
    memmove(body + off, body + off + stride, leaf->used_bytes - off - stride);
    leaf->n_records--;
    leaf->used_bytes -= stride;
    memset(body + leaf->used_bytes, 0, stride);

    if (leaf->n_records == 0) {
        u64 lb = leaf->block_no;
        bt_put(m, leaf);
        if (depth == 0) {
            // The single leaf is the root: the tree is now empty.
            rc = carafs_free_extent(m, lb, 1);
            if (rc != CARA_EOK) {
                return rc;
            }
            cn->tree_root = 0;
            return CARA_EOK;
        }
        rc = carafs_free_extent(m, lb, 1);
        if (rc != CARA_EOK) {
            return rc;
        }
        return dtree_collapse(m, cn, path, depth - 1);
    }

    u64 leaf_block = leaf->block_no;
    rc = bt_dirty(m, leaf);
    bt_put(m, leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    return bt_propagate(m, cn, CARAFS_BT_DIR, path, depth, leaf_block, false, 0, 0, 0);
}

// Move a packed, sorted inline-dirent blob into a fresh single leaf
// (one block always holds it — the inline item area is smaller than a
// leaf body). Sets cn->tree_root; the caller drops the inline item.
[[nodiscard]] int carafs_dtree_spill(struct CarafsMount *m, struct CarafsCnode *cn, const u8 *blob,
                                     u32 bytes)
{
    struct CarafsBtNode *leaf;
    int rc = bt_new(m, cn->block_no, 0, CARAFS_BT_DIR, &leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    u32 count = 0;
    for (u32 o = 0; o < bytes;) {
        const struct CarafsDirent *d = (const struct CarafsDirent *)(blob + o);
        o += dirent_stride(d->name_len);
        count++;
    }
    if (bytes) {
        memcpy(bt_body(leaf), blob, bytes);
    }
    leaf->n_records = (u16)count;
    leaf->used_bytes = bytes;
    rc = bt_dirty(m, leaf);
    u64 lb = leaf->block_no;
    bt_put(m, leaf);
    if (rc != CARA_EOK) {
        return rc;
    }
    cn->tree_root = lb;
    return CARA_EOK;
}

[[nodiscard]] int carafs_dtree_free_all(struct CarafsMount *m, struct CarafsCnode *cn)
{
    return tree_free_all(m, cn, CARAFS_BT_DIR, false);
}
