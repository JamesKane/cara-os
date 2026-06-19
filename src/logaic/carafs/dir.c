// SPDX-License-Identifier: BSD-2-Clause
//
// Directory layer (F3; CARAFS.md §3.6/§3.11).
//
// A directory's entries escalate transparently, mirroring file.c's
// storage escalation: an INLINE_DIRENTS item in the cnode (most
// drawers never leave this stage) → a single dirent leaf block → the
// (name_hash, collision_seq)-keyed B+tree in btree.c. The dirent wire
// format is identical at every stage, so promotion is a copy.
//
// This file owns the cnode-level glue the tree doesn't see: name
// hashing + collision sequencing, the NAME item and parent_cnode on a
// linked child, link_count accounting (hard/soft links), and the
// per-operation transaction bracket. The cnode id of an object IS its
// block number (§3.4); names merely point at it.

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

static u32 dirent_stride(u32 name_len)
{
    return Carafs_Align8(CARAFS_DIRENT_BASE + name_len);
}

// Composite dirent key order: (hash, then collision seq).
static int dkey_cmp(u64 ahi, u8 alo, u64 bhi, u8 blo)
{
    if (ahi != bhi) {
        return ahi < bhi ? -1 : 1;
    }
    if (alo != blo) {
        return alo < blo ? -1 : 1;
    }
    return 0;
}

// ---- Representation-agnostic directory primitives -----------------------------

// Probe the equal-hash run: *exists reports a folded-name match;
// *free_seq (non-null) gets the smallest unused collision seq.
static int dir_probe(struct CarafsMount *m, struct CarafsCnode *cn, u64 hash, const void *name,
                     u32 name_len, bool *exists, u8 *free_seq)
{
    if (cn->tree_root) {
        return carafs_dtree_scan(m, cn->tree_root, hash, name, name_len, exists, nullptr, free_seq);
    }
    u8 used[32];
    memset(used, 0, sizeof(used));
    bool found = false;
    u16 len = 0;
    u8 *p = carafs_item_find(cn, m->block_size, CARAFS_ITEM_INLINE_DIRENTS, &len);
    for (u32 off = 0; p && off < len;) {
        struct CarafsDirent *d = (struct CarafsDirent *)(p + off);
        if (d->hash == hash) {
            used[d->seq >> 3] |= (u8)(1u << (d->seq & 7));
            if (!found &&
                Carafs_NameEq(p + off + CARAFS_DIRENT_BASE, d->name_len, name, name_len)) {
                found = true;
            }
        }
        off += dirent_stride(d->name_len);
    }
    *exists = found;
    if (free_seq) {
        u32 fs = 0;
        while (fs < 256 && (used[fs >> 3] & (1u << (fs & 7)))) {
            fs++;
        }
        if (fs >= 256) {
            return CARA_EOVERFLOW;
        }
        *free_seq = (u8)fs;
    }
    return CARA_EOK;
}

// Folded-name lookup, returning the full dirent (with seq).
static int dir_find(struct CarafsMount *m, struct CarafsCnode *cn, u64 hash, const void *name,
                    u32 name_len, struct CarafsDirent *out)
{
    if (cn->tree_root) {
        return carafs_dtree_lookup(m, cn->tree_root, hash, name, name_len, out);
    }
    u16 len = 0;
    u8 *p = carafs_item_find(cn, m->block_size, CARAFS_ITEM_INLINE_DIRENTS, &len);
    for (u32 off = 0; p && off < len;) {
        struct CarafsDirent *d = (struct CarafsDirent *)(p + off);
        if (d->hash == hash &&
            Carafs_NameEq(p + off + CARAFS_DIRENT_BASE, d->name_len, name, name_len)) {
            *out = *d;
            return CARA_EOK;
        }
        off += dirent_stride(d->name_len);
    }
    return CARA_ENOTFOUND;
}

// Insert a fully-formed dirent, promoting inline → tree when the item
// area can no longer hold it. The caller guarantees (hash, seq) unique.
static int dir_insert_entry(struct CarafsMount *m, struct CarafsCnode *cn,
                            const struct CarafsDirent *de, const void *name)
{
    if (cn->tree_root) {
        return carafs_dtree_insert(m, cn, de, name);
    }
    u32 bs = m->block_size;
    u32 stride = dirent_stride(de->name_len);
    u16 cur_len = 0;
    u8 *cur = carafs_item_find(cn, bs, CARAFS_ITEM_INLINE_DIRENTS, &cur_len);
    u32 have = cur ? cur_len : 0;

    u8 *payload;
    if (carafs_item_resize(cn, bs, CARAFS_ITEM_INLINE_DIRENTS, (u16)(have + stride), &payload) ==
        CARA_EOK) {
        u32 off = 0;
        while (off < have) {
            struct CarafsDirent *d = (struct CarafsDirent *)(payload + off);
            if (dkey_cmp(d->hash, d->seq, de->hash, de->seq) > 0) {
                break;
            }
            off += dirent_stride(d->name_len);
        }
        memmove(payload + off + stride, payload + off, have - off);
        struct CarafsDirent *nd = (struct CarafsDirent *)(payload + off);
        *nd = *de;
        memcpy(payload + off + CARAFS_DIRENT_BASE, name, de->name_len);
        memset(payload + off + CARAFS_DIRENT_BASE + de->name_len, 0,
               stride - CARAFS_DIRENT_BASE - de->name_len);
        return CARA_EOK;
    }

    // Item area full: spill the (sorted) inline blob to a single leaf,
    // drop the item, then insert into the tree. `cur` lives in the
    // pinned cnode block; spill copies it out before the item goes.
    int rc = carafs_dtree_spill(m, cn, cur, have);
    if (rc != CARA_EOK) {
        return rc;
    }
    carafs_item_remove(cn, bs, CARAFS_ITEM_INLINE_DIRENTS);
    return carafs_dtree_insert(m, cn, de, name);
}

// Remove the entry keyed (hash, seq) from whichever representation holds it.
static int dir_remove_entry(struct CarafsMount *m, struct CarafsCnode *cn, u64 hash, u8 seq)
{
    if (cn->tree_root) {
        return carafs_dtree_remove(m, cn, hash, seq);
    }
    u32 bs = m->block_size;
    u16 len = 0;
    u8 *p = carafs_item_find(cn, bs, CARAFS_ITEM_INLINE_DIRENTS, &len);
    if (!p) {
        return CARA_ENOTFOUND;
    }
    u32 off = 0;
    bool found = false;
    while (off < len) {
        struct CarafsDirent *d = (struct CarafsDirent *)(p + off);
        if (d->hash == hash && d->seq == seq) {
            found = true;
            break;
        }
        off += dirent_stride(d->name_len);
    }
    if (!found) {
        return CARA_ENOTFOUND;
    }
    u32 stride = dirent_stride(((struct CarafsDirent *)(p + off))->name_len);
    memmove(p + off, p + off + stride, len - off - stride);
    u32 new_len = len - stride;
    if (new_len == 0) {
        carafs_item_remove(cn, bs, CARAFS_ITEM_INLINE_DIRENTS);
        return CARA_EOK;
    }
    u8 *np;
    return carafs_item_resize(cn, bs, CARAFS_ITEM_INLINE_DIRENTS, (u16)new_len, &np);
}

// Inline iteration: first entry with key strictly past the cursor, or
// the very first when the cursor has not started. The blob is sorted,
// so the first qualifying record is the answer.
static int dir_inline_next(struct CarafsMount *m, struct CarafsCnode *cn, bool started,
                           u64 cur_hash, u8 cur_seq, struct CarafsDirent *out, u8 *name_out)
{
    u16 len = 0;
    u8 *p = carafs_item_find(cn, m->block_size, CARAFS_ITEM_INLINE_DIRENTS, &len);
    for (u32 off = 0; p && off < len;) {
        struct CarafsDirent *d = (struct CarafsDirent *)(p + off);
        if (!started || dkey_cmp(d->hash, d->seq, cur_hash, cur_seq) > 0) {
            *out = *d;
            memcpy(name_out, p + off + CARAFS_DIRENT_BASE, d->name_len);
            return CARA_EOK;
        }
        off += dirent_stride(d->name_len);
    }
    return CARA_ENOTFOUND;
}

// Allocate a child cnode and link it under `name`: NAME item +
// parent_cnode on the child, dirent in `dir`, dir entry-count bumped.
// Runs inside the caller's open transaction. CARA_EEXIST if taken.
static int dir_link_new(struct CarafsMount *m, struct CarafsCnode *dir, u16 type, const void *name,
                        u32 name_len, u64 *child_out)
{
    u64 hash = Carafs_NameHash(name, name_len);
    bool exists;
    u8 seq;
    int rc = dir_probe(m, dir, hash, name, name_len, &exists, &seq);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (exists) {
        return CARA_EEXIST;
    }
    u64 child;
    rc = carafs_cnode_alloc(m, type, dir->block_no, &child);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsCnode *ccn;
    rc = carafs_cnode_get(m, child, &ccn);
    if (rc != CARA_EOK) {
        return rc;
    }
    u8 *np;
    rc = carafs_item_resize(ccn, m->block_size, CARAFS_ITEM_NAME, (u16)name_len, &np);
    if (rc == CARA_EOK) {
        memcpy(np, name, name_len);
        ccn->parent_cnode = dir->block_no;
        rc = carafs_cnode_dirty(m, ccn);
    }
    carafs_cnode_put(m, ccn);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsDirent de = {
        .hash = hash, .cnode = child, .type = (u8)type, .name_len = (u8)name_len, .seq = seq
    };
    rc = dir_insert_entry(m, dir, &de, name);
    if (rc != CARA_EOK) {
        return rc;
    }
    dir->size_bytes++;
    dir->modified_ns = carafs_now(m);
    dir->changed_ns = dir->modified_ns;
    *child_out = child;
    return CARA_EOK;
}

// Pin a cnode and require it to be a directory.
static int get_dir(struct CarafsMount *m, u64 dir, struct CarafsCnode **out)
{
    struct CarafsCnode *cn;
    int rc = carafs_cnode_get(m, dir, &cn);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (cn->type != CARAFS_T_DIR) {
        carafs_cnode_put(m, cn);
        return CARA_EINVAL;
    }
    *out = cn;
    return CARA_EOK;
}

// ---- Public API ---------------------------------------------------------------

[[nodiscard]] int Carafs_DirLookup(struct CarafsMount *m, u64 dir, const void *name, u32 name_len,
                                   u64 *cnode_out, u16 *type_out)
{
    if (!m || !m->mounted || !name || !Carafs_NameValid(name, name_len)) {
        return CARA_EINVAL;
    }
    struct CarafsCnode *cn;
    int rc = get_dir(m, dir, &cn);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsDirent de;
    rc = dir_find(m, cn, Carafs_NameHash(name, name_len), name, name_len, &de);
    carafs_cnode_put(m, cn);
    if (rc != CARA_EOK) {
        return rc == CARA_ENOTFOUND ? CARA_ENOENT : rc;
    }
    if (cnode_out) {
        *cnode_out = de.cnode;
    }
    if (type_out) {
        *type_out = de.type;
    }
    return CARA_EOK;
}

[[nodiscard]] int Carafs_DirCreate(struct CarafsMount *m, u64 dir, const void *name, u32 name_len,
                                   u16 type, u64 *cnode_out)
{
    if (!m || !cnode_out || !name || !Carafs_NameValid(name, name_len) ||
        (type != CARAFS_T_FILE && type != CARAFS_T_DIR && type != CARAFS_T_SYMLINK)) {
        return CARA_EINVAL;
    }
    int rc = carafs_op_begin(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsCnode *dcn;
    rc = get_dir(m, dir, &dcn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    u64 child;
    rc = dir_link_new(m, dcn, type, name, name_len, &child);
    if (rc == CARA_EOK) {
        rc = carafs_cnode_dirty(m, dcn);
    }
    carafs_cnode_put(m, dcn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    rc = carafs_op_commit(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    *cnode_out = child;
    return CARA_EOK;
}

[[nodiscard]] int Carafs_DirSymlink(struct CarafsMount *m, u64 dir, const void *name, u32 name_len,
                                    const void *target, u32 target_len, u64 *cnode_out)
{
    if (!m || !cnode_out || !name || !target || target_len == 0 || target_len > 0xFFFFu ||
        !Carafs_NameValid(name, name_len)) {
        return CARA_EINVAL;
    }
    int rc = carafs_op_begin(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsCnode *dcn;
    rc = get_dir(m, dir, &dcn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    u64 child;
    rc = dir_link_new(m, dcn, CARAFS_T_SYMLINK, name, name_len, &child);
    if (rc == CARA_EOK) {
        rc = carafs_cnode_dirty(m, dcn);
    }
    if (rc != CARA_EOK) {
        carafs_cnode_put(m, dcn);
        carafs_op_abort(m);
        return rc;
    }
    // Store the target on the child.
    struct CarafsCnode *ccn;
    rc = carafs_cnode_get(m, child, &ccn);
    if (rc == CARA_EOK) {
        u8 *tp;
        rc = carafs_item_resize(ccn, m->block_size, CARAFS_ITEM_SYMLINK_TARGET, (u16)target_len,
                                &tp);
        if (rc == CARA_EOK) {
            memcpy(tp, target, target_len);
            ccn->size_bytes = target_len;
            rc = carafs_cnode_dirty(m, ccn);
        }
        carafs_cnode_put(m, ccn);
    }
    carafs_cnode_put(m, dcn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    rc = carafs_op_commit(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    *cnode_out = child;
    return CARA_EOK;
}

[[nodiscard]] int Carafs_DirLink(struct CarafsMount *m, u64 dir, const void *name, u32 name_len,
                                 u64 target)
{
    if (!m || !name || !Carafs_NameValid(name, name_len)) {
        return CARA_EINVAL;
    }
    int rc = carafs_op_begin(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsCnode *dcn = nullptr;
    struct CarafsCnode *tcn = nullptr;
    u64 hash = Carafs_NameHash(name, name_len);
    bool exists = false;
    u8 seq = 0;
    struct CarafsDirent de;
    rc = get_dir(m, dir, &dcn);
    if (rc != CARA_EOK) {
        goto out;
    }
    rc = carafs_cnode_get(m, target, &tcn);
    if (rc != CARA_EOK) {
        goto out;
    }
    if (tcn->type != CARAFS_T_FILE) {
        rc = CARA_EINVAL; // no directory / symlink hard links (§3.11)
        goto out;
    }
    rc = dir_probe(m, dcn, hash, name, name_len, &exists, &seq);
    if (rc != CARA_EOK) {
        goto out;
    }
    if (exists) {
        rc = CARA_EEXIST;
        goto out;
    }
    de = (struct CarafsDirent){
        .hash = hash, .cnode = target, .type = (u8)tcn->type, .name_len = (u8)name_len, .seq = seq
    };
    rc = dir_insert_entry(m, dcn, &de, name);
    if (rc != CARA_EOK) {
        goto out;
    }
    if (tcn->link_count == 1) {
        tcn->parent_cnode = 0; // no longer single-parent (§3.11)
    }
    tcn->link_count++;
    tcn->changed_ns = carafs_now(m);
    dcn->size_bytes++;
    dcn->modified_ns = carafs_now(m);
    dcn->changed_ns = dcn->modified_ns;
    rc = carafs_cnode_dirty(m, tcn);
    if (rc == CARA_EOK) {
        rc = carafs_cnode_dirty(m, dcn);
    }
out:
    if (tcn) {
        carafs_cnode_put(m, tcn);
    }
    if (dcn) {
        carafs_cnode_put(m, dcn);
    }
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    return carafs_op_commit(m);
}

[[nodiscard]] int Carafs_DirRemove(struct CarafsMount *m, u64 dir, const void *name, u32 name_len)
{
    if (!m || !name || !Carafs_NameValid(name, name_len)) {
        return CARA_EINVAL;
    }
    int rc = carafs_op_begin(m);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsCnode *dcn = nullptr;
    struct CarafsCnode *ccn = nullptr;
    struct CarafsDirent de;
    rc = get_dir(m, dir, &dcn);
    if (rc != CARA_EOK) {
        goto out;
    }
    rc = dir_find(m, dcn, Carafs_NameHash(name, name_len), name, name_len, &de);
    if (rc != CARA_EOK) {
        rc = (rc == CARA_ENOTFOUND) ? CARA_ENOENT : rc;
        goto out;
    }
    rc = carafs_cnode_get(m, de.cnode, &ccn);
    if (rc != CARA_EOK) {
        goto out;
    }
    if (ccn->type == CARAFS_T_DIR && ccn->size_bytes != 0) {
        rc = CARA_ENOTEMPTY;
        goto out;
    }
    rc = dir_remove_entry(m, dcn, de.hash, de.seq);
    if (rc != CARA_EOK) {
        goto out;
    }
    dcn->size_bytes--;
    dcn->modified_ns = carafs_now(m);
    dcn->changed_ns = dcn->modified_ns;
    if (ccn->link_count <= 1) {
        rc = carafs_cnode_free_locked(m, ccn); // frees storage + tombstone
    } else {
        ccn->link_count--;
        ccn->changed_ns = carafs_now(m);
        rc = carafs_cnode_dirty(m, ccn);
    }
    if (rc == CARA_EOK) {
        rc = carafs_cnode_dirty(m, dcn);
    }
out:
    if (ccn) {
        carafs_cnode_put(m, ccn);
    }
    if (dcn) {
        carafs_cnode_put(m, dcn);
    }
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    return carafs_op_commit(m);
}

[[nodiscard]] int Carafs_DirNext(struct CarafsMount *m, u64 dir, struct CarafsDirCursor *cur,
                                 struct CarafsDirEntry *out)
{
    if (!m || !m->mounted || !cur || !out) {
        return CARA_EINVAL;
    }
    struct CarafsCnode *cn;
    int rc = get_dir(m, dir, &cn);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct CarafsDirent de;
    if (cn->tree_root) {
        rc = carafs_dtree_next(m, cn->tree_root, cur->hash, cur->seq, cur->started, &de, out->name);
    } else {
        rc = dir_inline_next(m, cn, cur->started, cur->hash, cur->seq, &de, out->name);
    }
    carafs_cnode_put(m, cn);
    if (rc != CARA_EOK) {
        return rc == CARA_ENOTFOUND ? CARA_ENOENT : rc;
    }
    out->cnode = de.cnode;
    out->type = de.type;
    out->name_len = de.name_len;
    cur->hash = de.hash;
    cur->seq = de.seq;
    cur->started = true;
    return CARA_EOK;
}

[[nodiscard]] int Carafs_SymlinkRead(struct CarafsMount *m, u64 cnode, void *buf, usize buflen,
                                     usize *len_out)
{
    if (!m || !m->mounted || !buf || !len_out) {
        return CARA_EINVAL;
    }
    struct CarafsCnode *cn;
    int rc = carafs_cnode_get(m, cnode, &cn);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (cn->type != CARAFS_T_SYMLINK) {
        carafs_cnode_put(m, cn);
        return CARA_EINVAL;
    }
    u16 len = 0;
    u8 *p = carafs_item_find(cn, m->block_size, CARAFS_ITEM_SYMLINK_TARGET, &len);
    usize n = (len < buflen) ? len : buflen;
    if (p && n) {
        memcpy(buf, p, n);
    }
    *len_out = len;
    carafs_cnode_put(m, cn);
    return CARA_EOK;
}
