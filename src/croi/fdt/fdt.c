// SPDX-License-Identifier: BSD-2-Clause
//
// FDT parser core: header validation, structure-block token reader, and
// node-skipping helper. The bulk of the property and path APIs live in
// sibling translation units; this file only provides what they all need.

#include "fdt_internal.h"

#include <cara/fdt.h>
#include <cara/types.h>

// Header layout per Devicetree Specification v0.4 §5.2.
struct FdtRawHeader {
    u32 magic;
    u32 totalsize;
    u32 off_dt_struct;
    u32 off_dt_strings;
    u32 off_mem_rsvmap;
    u32 version;
    u32 last_comp_version;
    u32 boot_cpuid_phys;
    u32 size_dt_strings;
    u32 size_dt_struct;
};

// We require version >= 17 (which adds size_dt_struct), and we accept
// blobs whose last_comp_version <= 16 (the "16+strings_size" boundary
// we know how to read).
#define FDT_REQUIRED_VERSION   17u
#define FDT_MAX_LAST_COMP      16u

[[nodiscard]] int Fdt_Open(struct Fdt *out, const void *blob)
{
    if (!out || !blob) {
        return CARA_EINVAL;
    }

    const u8 *bytes = (const u8 *)blob;

    // Read the header big-endian; do not assume alignment.
    if (((uptr)blob & 3u) != 0) {
        // The DTB spec mandates 8-byte alignment; reject anything looser.
        return CARA_EINVAL;
    }

    u32 magic       = fdt_be32(bytes + 0);
    u32 totalsize   = fdt_be32(bytes + 4);
    u32 off_struct  = fdt_be32(bytes + 8);
    u32 off_strings = fdt_be32(bytes + 12);
    u32 off_rsvmap  = fdt_be32(bytes + 16);
    u32 version     = fdt_be32(bytes + 20);
    u32 last_comp   = fdt_be32(bytes + 24);
    u32 boot_cpu    = fdt_be32(bytes + 28);
    u32 size_strings = fdt_be32(bytes + 32);
    u32 size_struct  = fdt_be32(bytes + 36);

    if (magic != FDT_MAGIC) {
        return CARA_EBADMAGIC;
    }
    if (version < FDT_REQUIRED_VERSION) {
        return CARA_EBADVERSION;
    }
    if (last_comp > FDT_MAX_LAST_COMP) {
        return CARA_EBADVERSION;
    }
    if (totalsize < sizeof(struct FdtRawHeader)) {
        return CARA_EINVAL;
    }

    // Bounds: each region must fit inside totalsize without overflowing.
    if (off_struct > totalsize || size_struct > totalsize - off_struct) {
        return CARA_ERANGE;
    }
    if (off_strings > totalsize || size_strings > totalsize - off_strings) {
        return CARA_ERANGE;
    }
    if (off_rsvmap > totalsize) {
        return CARA_ERANGE;
    }
    // Structure block must be 4-byte aligned and have room for at least
    // the FDT_END terminator (4 bytes).
    if ((off_struct & 3u) != 0 || size_struct < 4u) {
        return CARA_ERANGE;
    }
    // Reservation block is at least 16 bytes (one terminator entry) and
    // 8-byte aligned per spec.
    if ((off_rsvmap & 7u) != 0 || off_rsvmap + 16u > totalsize) {
        return CARA_ERANGE;
    }

    out->blob = bytes;
    out->totalsize = totalsize;
    out->off_struct = off_struct;
    out->size_struct = size_struct;
    out->off_strings = off_strings;
    out->size_strings = size_strings;
    out->off_rsvmap = off_rsvmap;
    out->version = version;
    out->boot_cpuid_phys = boot_cpu;
    return CARA_EOK;
}

[[nodiscard]] int fdt_read_token(const struct Fdt *fdt, u32 off, u32 *token_out,
                                 u32 *payload_off_out, u32 *next_off_out)
{
    if (off > fdt->size_struct || (fdt->size_struct - off) < 4u) {
        return CARA_ERANGE;
    }
    if ((off & 3u) != 0) {
        return CARA_EINVAL;
    }

    const u8 *p = fdt->blob + fdt->off_struct;
    u32 token = fdt_be32(p + off);
    u32 payload = off + 4u;
    u32 next = payload;

    switch (token) {
    case FDT_BEGIN_NODE: {
        // Name is NUL-terminated, then padded to 4-byte boundary.
        u32 i = 0;
        while (payload + i < fdt->size_struct && p[payload + i] != 0) {
            i++;
        }
        if (payload + i >= fdt->size_struct) {
            return CARA_ERANGE;
        }
        next = fdt_align4(payload + i + 1u);
        if (next > fdt->size_struct) {
            return CARA_ERANGE;
        }
        break;
    }
    case FDT_PROP: {
        // u32 len, u32 nameoff, then len bytes padded to 4.
        if ((fdt->size_struct - payload) < 8u) {
            return CARA_ERANGE;
        }
        u32 len = fdt_be32(p + payload);
        u32 data_off = payload + 8u;
        u32 padded = fdt_align4(len);
        if (padded < len) {
            return CARA_EOVERFLOW;
        }
        if (data_off > fdt->size_struct
            || (fdt->size_struct - data_off) < padded) {
            return CARA_ERANGE;
        }
        next = data_off + padded;
        break;
    }
    case FDT_END_NODE:
    case FDT_NOP:
    case FDT_END:
        break;
    default:
        return CARA_EINVAL;
    }

    if (token_out) {
        *token_out = token;
    }
    if (payload_off_out) {
        *payload_off_out = payload;
    }
    if (next_off_out) {
        *next_off_out = next;
    }
    return CARA_EOK;
}

[[nodiscard]] int fdt_skip_node(const struct Fdt *fdt, u32 node_off,
                                u32 *after_end_off_out)
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
    int depth = 1;
    u32 off = next;
    while (depth > 0) {
        rc = fdt_read_token(fdt, off, &token, nullptr, &next);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (token == FDT_BEGIN_NODE) {
            depth++;
        } else if (token == FDT_END_NODE) {
            depth--;
        } else if (token == FDT_END) {
            return CARA_ERANGE;
        }
        off = next;
    }
    if (after_end_off_out) {
        *after_end_off_out = off;
    }
    return CARA_EOK;
}

[[nodiscard]] const char *fdt_strings_get(const struct Fdt *fdt, u32 nameoff)
{
    if (nameoff >= fdt->size_strings) {
        return nullptr;
    }
    const u8 *base = fdt->blob + fdt->off_strings;
    // Verify NUL-terminated within the strings block.
    for (u32 i = nameoff; i < fdt->size_strings; i++) {
        if (base[i] == 0) {
            return (const char *)(base + nameoff);
        }
    }
    return nullptr;
}

[[nodiscard]] u32 Fdt_Root(const struct Fdt *fdt)
{
    // Root is the first BEGIN_NODE in the structure block; skip leading
    // NOPs (the spec permits them anywhere). On a well-formed blob it's
    // at offset 0.
    u32 off = 0;
    while (off < fdt->size_struct) {
        u32 token = 0;
        u32 next = 0;
        if (fdt_read_token(fdt, off, &token, nullptr, &next) != CARA_EOK) {
            return 0;
        }
        if (token == FDT_BEGIN_NODE) {
            return off;
        }
        if (token != FDT_NOP) {
            return 0;
        }
        off = next;
    }
    return 0;
}

[[nodiscard]] const char *Fdt_NodeName(const struct Fdt *fdt, u32 node)
{
    u32 token = 0;
    u32 payload = 0;
    if (fdt_read_token(fdt, node, &token, &payload, nullptr) != CARA_EOK) {
        return nullptr;
    }
    if (token != FDT_BEGIN_NODE) {
        return nullptr;
    }
    return (const char *)(fdt->blob + fdt->off_struct + payload);
}

[[nodiscard]] int Fdt_RsvIter(const struct Fdt *fdt, u32 *cursor_inout,
                              u64 *base_out, u64 *size_out)
{
    if (!cursor_inout) {
        return CARA_EINVAL;
    }
    u32 cursor = *cursor_inout;
    u32 entry_off = fdt->off_rsvmap + cursor * 16u;
    if (entry_off + 16u > fdt->totalsize) {
        return CARA_ERANGE;
    }
    u64 base = fdt_be64(fdt->blob + entry_off);
    u64 size = fdt_be64(fdt->blob + entry_off + 8u);
    if (base == 0 && size == 0) {
        return CARA_ENOTFOUND;
    }
    if (base_out) {
        *base_out = base;
    }
    if (size_out) {
        *size_out = size;
    }
    *cursor_inout = cursor + 1u;
    return CARA_EOK;
}
