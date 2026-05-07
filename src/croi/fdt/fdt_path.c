// SPDX-License-Identifier: BSD-2-Clause
//
// FDT navigation: child iteration, path resolution, compatible search.

#include "fdt_internal.h"

#include <cara/fdt.h>
#include <cara/types.h>

// Compare a NUL-terminated path segment against a node name from the blob.
// The blob's name may be of the form "name@unit-addr"; the segment may
// either include or omit the unit-address suffix. Match if either:
//  - segment_len == name_len and bytes match exactly, or
//  - segment ends before '@' in the name and bytes match up to that.
[[nodiscard]] static bool fdt_segment_matches(const char *segment, u32 seg_len,
                                              const char *name)
{
    u32 i = 0;
    while (i < seg_len && name[i] != 0 && name[i] == segment[i]) {
        i++;
    }
    if (i != seg_len) {
        return false;
    }
    return name[i] == 0 || name[i] == '@';
}

// Advance from a BEGIN_NODE offset to the offset of the first interior token
// (i.e. immediately after the node name).
[[nodiscard]] static int fdt_node_body(const struct Fdt *fdt, u32 node_off,
                                       u32 *body_off_out)
{
    u32 token = 0;
    u32 next = 0;
    int rc = fdt_read_token(fdt, node_off, &token, nullptr, &next);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (token != FDT_BEGIN_NODE) {
        return CARA_EINVAL;
    }
    *body_off_out = next;
    return CARA_EOK;
}

[[nodiscard]] int Fdt_ChildIter(const struct Fdt *fdt, u32 parent,
                                u32 *cursor_inout, u32 *node_off_out)
{
    if (!cursor_inout || !node_off_out) {
        return CARA_EINVAL;
    }

    u32 off = 0;
    if (*cursor_inout == 0) {
        int rc = fdt_node_body(fdt, parent, &off);
        if (rc != CARA_EOK) {
            return rc;
        }
    } else {
        off = *cursor_inout;
    }

    while (off < fdt->size_struct) {
        u32 token = 0;
        u32 payload = 0;
        u32 next = 0;
        int rc = fdt_read_token(fdt, off, &token, &payload, &next);
        if (rc != CARA_EOK) {
            return rc;
        }

        if (token == FDT_PROP || token == FDT_NOP) {
            off = next;
            continue;
        }
        if (token == FDT_BEGIN_NODE) {
            *node_off_out = off;
            // Skip over this node so the next call resumes at its sibling.
            u32 after = 0;
            rc = fdt_skip_node(fdt, off, &after);
            if (rc != CARA_EOK) {
                return rc;
            }
            *cursor_inout = after;
            return CARA_EOK;
        }
        if (token == FDT_END_NODE || token == FDT_END) {
            return CARA_ENOTFOUND;
        }
        return CARA_EINVAL;
    }
    return CARA_ENOTFOUND;
}

// Resolve one path segment within `parent`. `seg` and `seg_len` describe the
// segment without leading or trailing '/'.
[[nodiscard]] static int fdt_resolve_segment(const struct Fdt *fdt, u32 parent,
                                             const char *seg, u32 seg_len,
                                             u32 *out)
{
    u32 cursor = 0;
    u32 child = 0;
    int rc = 0;
    while ((rc = Fdt_ChildIter(fdt, parent, &cursor, &child)) == CARA_EOK) {
        const char *name = Fdt_NodeName(fdt, child);
        if (name && fdt_segment_matches(seg, seg_len, name)) {
            *out = child;
            return CARA_EOK;
        }
    }
    return CARA_ENOTFOUND;
}

[[nodiscard]] int Fdt_ResolvePath(const struct Fdt *fdt, const char *path,
                                  u32 *node_off_out)
{
    if (!path || !node_off_out) {
        return CARA_EINVAL;
    }
    if (path[0] != '/') {
        return CARA_EINVAL;
    }

    u32 cur = Fdt_Root(fdt);
    if (path[1] == 0) {
        *node_off_out = cur;
        return CARA_EOK;
    }

    u32 i = 1;
    while (path[i] != 0) {
        u32 start = i;
        while (path[i] != 0 && path[i] != '/') {
            i++;
        }
        u32 seg_len = i - start;
        if (seg_len == 0) {
            // Empty segment ("//"): treat as malformed.
            return CARA_EINVAL;
        }
        u32 next = 0;
        int rc = fdt_resolve_segment(fdt, cur, path + start, seg_len, &next);
        if (rc != CARA_EOK) {
            return rc;
        }
        cur = next;
        if (path[i] == '/') {
            i++;
        }
    }

    *node_off_out = cur;
    return CARA_EOK;
}

[[nodiscard]] int Fdt_FindByCompatible(const struct Fdt *fdt,
                                       const char *compat,
                                       u32 *node_off_inout)
{
    if (!compat || !node_off_inout) {
        return CARA_EINVAL;
    }

    u32 start = *node_off_inout;
    u32 off = 0;

    if (start == 0) {
        // First call: start scanning from the very beginning.
        off = 0;
    } else {
        // Resume after the previous match: skip the node we returned last
        // time, and continue from its successor token.
        int rc = fdt_skip_node(fdt, start, &off);
        if (rc != CARA_EOK) {
            return rc;
        }
    }

    while (off < fdt->size_struct) {
        u32 token = 0;
        u32 next = 0;
        int rc = fdt_read_token(fdt, off, &token, nullptr, &next);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (token == FDT_BEGIN_NODE) {
            if (Fdt_NodeIsCompatible(fdt, off, compat)) {
                *node_off_inout = off;
                return CARA_EOK;
            }
        } else if (token == FDT_END) {
            break;
        }
        off = next;
    }
    return CARA_ENOTFOUND;
}
