// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS extended attributes (epic L11; CARAFS.md §3.10).
//
// All of a cnode's attributes live in one CARAFS_ITEM_XATTR item in the
// cnode's TLV area — a packed list of records:
//
//   record ::= u8 name_len | u8 flags | u16 val_len(LE) | name[] | val[]
//
// over the generic carafs_item_* helpers. v0 is inline-only: a value
// that will not fit the cnode item area is rejected with CARA_EOVERFLOW
// (the on-disk format reserves CARAFS_ITEM_XATTR_OVERFLOW + a
// CarafsXattrBlock chain for the spill case, built in a later slice).
// Defining the record layout inside the already-reserved item kind
// completes the format; it does not change it (no volume carries an
// xattr item yet).

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

// Locate the record for (name, name_len) in a packed XATTR item payload
// [p, p+plen). Returns its byte offset, or plen when absent; *rsize_out
// (non-null) gets the matched record's total byte size.
static u32 xattr_find(const u8 *p, u32 plen, const void *name, u32 name_len, u32 *rsize_out)
{
    u32 off = 0;
    while (off + 4 <= plen) {
        u32 rn = p[off];
        u32 rv = (u32)p[off + 2] | ((u32)p[off + 3] << 8);
        u32 rsize = 4 + rn + rv;
        if (off + rsize > plen) {
            break; // malformed tail; stop scanning
        }
        if (rn == name_len && memcmp(p + off + 4, name, name_len) == 0) {
            if (rsize_out) {
                *rsize_out = rsize;
            }
            return off;
        }
        off += rsize;
    }
    return plen;
}

[[nodiscard]] int Carafs_XattrGet(struct CarafsMount *m, u64 cnode, const void *name, u32 name_len,
                                  void *buf, usize buflen, usize *len_out)
{
    if (!m || !m->mounted || !name || name_len == 0 || name_len > CARAFS_XATTR_NAME_MAX) {
        return CARA_EINVAL;
    }
    struct CarafsCnode *cn;
    int rc = carafs_cnode_get(m, cnode, &cn);
    if (rc != CARA_EOK) {
        return rc;
    }
    rc = CARA_ENOENT;
    u16 plen = 0;
    const u8 *p = carafs_item_find(cn, m->block_size, CARAFS_ITEM_XATTR, &plen);
    if (p) {
        u32 off = xattr_find(p, plen, name, name_len, nullptr);
        if (off < plen) {
            u32 rn = p[off];
            u32 rv = (u32)p[off + 2] | ((u32)p[off + 3] << 8);
            const u8 *val = p + off + 4 + rn;
            if (len_out) {
                *len_out = rv;
            }
            if (buf && buflen) {
                u32 n = (rv < buflen) ? rv : (u32)buflen;
                memcpy(buf, val, n);
            }
            rc = CARA_EOK;
        }
    }
    carafs_cnode_put(m, cn);
    return rc;
}

[[nodiscard]] int Carafs_XattrSet(struct CarafsMount *m, u64 cnode, const void *name, u32 name_len,
                                  const void *val, u32 val_len)
{
    if (!m || !name || name_len == 0 || name_len > CARAFS_XATTR_NAME_MAX ||
        val_len > CARAFS_XATTR_VALUE_MAX || (val_len && !val)) {
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

    u16 plen = 0;
    u8 *p = carafs_item_find(cn, m->block_size, CARAFS_ITEM_XATTR, &plen);
    u32 base_len = 0; // item length after dropping any existing record for `name`
    if (p) {
        base_len = plen;
        u32 rsize = 0;
        u32 off = xattr_find(p, plen, name, name_len, &rsize);
        if (off < plen) {
            // Close the gap in place; the resize below truncates the tail.
            memmove(p + off, p + off + rsize, (u32)plen - off - rsize);
            base_len = (u32)plen - rsize;
        }
    }

    u32 newrec = 4 + name_len + val_len;
    if (base_len + newrec > CARAFS_XATTR_VALUE_MAX) {
        carafs_cnode_put(m, cn);
        carafs_op_abort(m);
        return CARA_EOVERFLOW;
    }
    u8 *np;
    rc = carafs_item_resize(cn, m->block_size, CARAFS_ITEM_XATTR, (u16)(base_len + newrec), &np);
    if (rc != CARA_EOK) {
        carafs_cnode_put(m, cn);
        carafs_op_abort(m);
        return rc; // CARA_EOVERFLOW: does not fit inline (overflow chain TBD)
    }
    // Append the (replacement) record at base_len. resize preserved the
    // gap-closed records in [0, base_len); everything past it is ours.
    u8 *w = np + base_len;
    w[0] = (u8)name_len;
    w[1] = 0; // flags
    w[2] = (u8)(val_len & 0xFFu);
    w[3] = (u8)((val_len >> 8) & 0xFFu);
    memcpy(w + 4, name, name_len);
    if (val_len) {
        memcpy(w + 4 + name_len, val, val_len);
    }

    rc = carafs_cnode_dirty(m, cn);
    carafs_cnode_put(m, cn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    return carafs_op_commit(m);
}

[[nodiscard]] int Carafs_XattrRemove(struct CarafsMount *m, u64 cnode, const void *name,
                                     u32 name_len)
{
    if (!m || !name || name_len == 0 || name_len > CARAFS_XATTR_NAME_MAX) {
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

    u16 plen = 0;
    u8 *p = carafs_item_find(cn, m->block_size, CARAFS_ITEM_XATTR, &plen);
    u32 rsize = 0;
    u32 off = p ? xattr_find(p, plen, name, name_len, &rsize) : 0;
    if (!p || off >= plen) {
        carafs_cnode_put(m, cn);
        carafs_op_abort(m);
        return CARA_ENOENT;
    }
    memmove(p + off, p + off + rsize, (u32)plen - off - rsize);
    u32 base_len = (u32)plen - rsize;
    if (base_len == 0) {
        carafs_item_remove(cn, m->block_size, CARAFS_ITEM_XATTR);
    } else {
        u8 *np;
        rc = carafs_item_resize(cn, m->block_size, CARAFS_ITEM_XATTR, (u16)base_len, &np);
        if (rc != CARA_EOK) {
            carafs_cnode_put(m, cn);
            carafs_op_abort(m);
            return rc;
        }
    }

    rc = carafs_cnode_dirty(m, cn);
    carafs_cnode_put(m, cn);
    if (rc != CARA_EOK) {
        carafs_op_abort(m);
        return rc;
    }
    return carafs_op_commit(m);
}
