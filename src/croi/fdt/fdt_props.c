// SPDX-License-Identifier: BSD-2-Clause
//
// FDT property accessors. Walks the structure block forward from a node
// offset, matches property names against the strings block, and decodes the
// payload into the requested shape.

#include "fdt_internal.h"

#include <cara/fdt.h>
#include <cara/types.h>

// We deliberately don't pull in <string.h> here. The whole parser must
// build under freestanding for the kernel target.
static int fdt_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (int)(u8)*a - (int)(u8)*b;
}

// Locate the named property under `node_off`. On success writes the
// structure-block offset of the property's data bytes and the data length.
[[nodiscard]] static int fdt_find_prop(const struct Fdt *fdt, u32 node_off,
                                       const char *name, u32 *data_off_out,
                                       u32 *len_out)
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

    u32 off = next;
    while (off < fdt->size_struct) {
        u32 payload = 0;
        rc = fdt_read_token(fdt, off, &token, &payload, &next);
        if (rc != CARA_EOK) {
            return rc;
        }

        if (token == FDT_PROP) {
            u32 len = fdt_be32(fdt->blob + fdt->off_struct + payload);
            u32 nameoff = fdt_be32(fdt->blob + fdt->off_struct + payload + 4u);
            const char *prop_name = fdt_strings_get(fdt, nameoff);
            if (!prop_name) {
                return CARA_EINVAL;
            }
            if (fdt_strcmp(prop_name, name) == 0) {
                if (data_off_out) {
                    *data_off_out = payload + 8u;
                }
                if (len_out) {
                    *len_out = len;
                }
                return CARA_EOK;
            }
        } else if (token == FDT_BEGIN_NODE || token == FDT_END_NODE) {
            // Properties come before children (spec §5.4); past either means
            // we've left this node's property list.
            return CARA_ENOTFOUND;
        } else if (token == FDT_END) {
            return CARA_ENOTFOUND;
        }
        off = next;
    }
    return CARA_ENOTFOUND;
}

[[nodiscard]] int Fdt_PropRaw(const struct Fdt *fdt, u32 node, const char *name,
                              const void **bytes_out, u32 *len_out)
{
    u32 data_off = 0;
    u32 len = 0;
    int rc = fdt_find_prop(fdt, node, name, &data_off, &len);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (bytes_out) {
        *bytes_out = fdt->blob + fdt->off_struct + data_off;
    }
    if (len_out) {
        *len_out = len;
    }
    return CARA_EOK;
}

[[nodiscard]] int Fdt_PropU32(const struct Fdt *fdt, u32 node, const char *name,
                              u32 *out)
{
    const void *bytes = nullptr;
    u32 len = 0;
    int rc = Fdt_PropRaw(fdt, node, name, &bytes, &len);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (len != 4u) {
        return CARA_EINVAL;
    }
    if (out) {
        *out = fdt_be32((const u8 *)bytes);
    }
    return CARA_EOK;
}

[[nodiscard]] int Fdt_PropU64(const struct Fdt *fdt, u32 node, const char *name,
                              u64 *out)
{
    const void *bytes = nullptr;
    u32 len = 0;
    int rc = Fdt_PropRaw(fdt, node, name, &bytes, &len);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (len != 8u) {
        return CARA_EINVAL;
    }
    if (out) {
        *out = fdt_be64((const u8 *)bytes);
    }
    return CARA_EOK;
}

[[nodiscard]] const char *Fdt_PropStr(const struct Fdt *fdt, u32 node,
                                      const char *name)
{
    const void *bytes = nullptr;
    u32 len = 0;
    if (Fdt_PropRaw(fdt, node, name, &bytes, &len) != CARA_EOK) {
        return nullptr;
    }
    if (len == 0) {
        return nullptr;
    }
    const char *s = (const char *)bytes;
    if (s[len - 1] != 0) {
        return nullptr;
    }
    return s;
}

[[nodiscard]] int Fdt_PropStrIter(const struct Fdt *fdt, u32 node,
                                  const char *name, u32 *cursor_inout,
                                  const char **str_out)
{
    if (!cursor_inout) {
        return CARA_EINVAL;
    }
    const void *bytes = nullptr;
    u32 len = 0;
    int rc = Fdt_PropRaw(fdt, node, name, &bytes, &len);
    if (rc != CARA_EOK) {
        return rc;
    }
    u32 cursor = *cursor_inout;
    if (cursor >= len) {
        return CARA_ENOTFOUND;
    }
    const char *base = (const char *)bytes;
    if (base[len - 1] != 0) {
        return CARA_EINVAL;
    }
    const char *s = base + cursor;
    u32 i = cursor;
    while (i < len && base[i] != 0) {
        i++;
    }
    if (i >= len) {
        return CARA_EINVAL;
    }
    *cursor_inout = i + 1u;
    if (str_out) {
        *str_out = s;
    }
    return CARA_EOK;
}

[[nodiscard]] bool Fdt_NodeIsCompatible(const struct Fdt *fdt, u32 node,
                                        const char *compat)
{
    u32 cursor = 0;
    const char *s = nullptr;
    int rc = 0;
    while ((rc = Fdt_PropStrIter(fdt, node, "compatible", &cursor, &s))
           == CARA_EOK) {
        if (fdt_strcmp(s, compat) == 0) {
            return true;
        }
    }
    return false;
}

// Walk from root to find the parent of `child_off`. Returns CARA_ENOTFOUND
// if the child is the root (root has no parent) or is not present.
[[nodiscard]] static int fdt_find_parent(const struct Fdt *fdt, u32 child_off,
                                         u32 *parent_off_out)
{
    enum { MAX_DEPTH = 16 };
    u32 ancestors[MAX_DEPTH];
    int depth = -1;
    u32 off = 0;

    while (off < fdt->size_struct) {
        u32 token = 0;
        u32 next = 0;
        int rc = fdt_read_token(fdt, off, &token, nullptr, &next);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (token == FDT_BEGIN_NODE) {
            if (off == child_off) {
                if (depth < 0) {
                    return CARA_ENOTFOUND;
                }
                if (parent_off_out) {
                    *parent_off_out = ancestors[depth];
                }
                return CARA_EOK;
            }
            if (depth + 1 >= MAX_DEPTH) {
                return CARA_ERANGE;
            }
            ancestors[++depth] = off;
        } else if (token == FDT_END_NODE) {
            if (depth < 0) {
                return CARA_EINVAL;
            }
            depth--;
        } else if (token == FDT_END) {
            break;
        }
        off = next;
    }
    return CARA_ENOTFOUND;
}

static void fdt_get_parent_cells(const struct Fdt *fdt, u32 node_off,
                                 u32 *addr_cells_out, u32 *size_cells_out)
{
    // Defaults from Devicetree Specification §2.3.5.
    *addr_cells_out = 2;
    *size_cells_out = 1;

    u32 parent_off = 0;
    if (fdt_find_parent(fdt, node_off, &parent_off) != CARA_EOK) {
        return;
    }
    u32 v = 0;
    if (Fdt_PropU32(fdt, parent_off, "#address-cells", &v) == CARA_EOK) {
        *addr_cells_out = v;
    }
    if (Fdt_PropU32(fdt, parent_off, "#size-cells", &v) == CARA_EOK) {
        *size_cells_out = v;
    }
}

[[nodiscard]] int Fdt_PropReg(const struct Fdt *fdt, u32 node, u32 index,
                              u64 *base_out, u64 *size_out)
{
    u32 addr_cells = 0;
    u32 size_cells = 0;
    fdt_get_parent_cells(fdt, node, &addr_cells, &size_cells);

    if (addr_cells == 0 || addr_cells > 2 || size_cells > 2) {
        return CARA_EINVAL;
    }

    const void *bytes = nullptr;
    u32 len = 0;
    int rc = Fdt_PropRaw(fdt, node, "reg", &bytes, &len);
    if (rc != CARA_EOK) {
        return rc;
    }

    u32 cell_bytes = (addr_cells + size_cells) * 4u;
    if (cell_bytes == 0) {
        return CARA_EINVAL;
    }
    if (len % cell_bytes != 0) {
        return CARA_EINVAL;
    }
    u32 entries = len / cell_bytes;
    if (index >= entries) {
        return CARA_ENOTFOUND;
    }

    const u8 *p = (const u8 *)bytes + index * cell_bytes;
    u64 base = 0;
    for (u32 i = 0; i < addr_cells; i++) {
        base = (base << 32) | (u64)fdt_be32(p + i * 4u);
    }
    u64 size = 0;
    for (u32 i = 0; i < size_cells; i++) {
        size = (size << 32) | (u64)fdt_be32(p + addr_cells * 4u + i * 4u);
    }
    if (base_out) {
        *base_out = base;
    }
    if (size_out) {
        *size_out = size;
    }
    return CARA_EOK;
}
