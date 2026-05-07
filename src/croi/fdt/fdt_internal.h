// SPDX-License-Identifier: BSD-2-Clause
//
// Internal helpers for the FDT parser. Not exported.

#ifndef CARA_FDT_INTERNAL_H
#define CARA_FDT_INTERNAL_H

#include <cara/fdt.h>
#include <cara/types.h>

// FDT magic and tokens (Devicetree Specification §5.1, §5.4).
#define FDT_MAGIC 0xD00DFEEDu

#define FDT_BEGIN_NODE 0x00000001u
#define FDT_END_NODE   0x00000002u
#define FDT_PROP       0x00000003u
#define FDT_NOP        0x00000004u
#define FDT_END        0x00000009u

// Big-endian u32 → host. The blob's bytes may be unaligned, so read byte by
// byte; the compiler will collapse this to a load + bswap on most targets
// and we don't pay for misaligned hardware traps on RISC-V.
static inline u32 fdt_be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static inline u64 fdt_be64(const u8 *p)
{
    return ((u64)fdt_be32(p) << 32) | (u64)fdt_be32(p + 4);
}

// Round up to the next multiple of 4.
static inline u32 fdt_align4(u32 x)
{
    return (x + 3u) & ~3u;
}

// Read one structure-block token at `off` (offset into the structure block).
// Returns the token value; *next_off becomes the offset of the next token
// (after any payload). Returns CARA_EOK or a negative error.
[[nodiscard]] int fdt_read_token(const struct Fdt *fdt, u32 off,
                                 u32 *token_out, u32 *payload_off_out,
                                 u32 *next_off_out);

// Skip the current node (whose BEGIN_NODE starts at `node_off`) and return
// the offset of the token immediately after its matching END_NODE.
[[nodiscard]] int fdt_skip_node(const struct Fdt *fdt, u32 node_off,
                                u32 *after_end_off_out);

// Look up a string in the strings block by offset. Returns nullptr if the
// offset is out of range or the string is not NUL-terminated within the
// strings block.
[[nodiscard]] const char *fdt_strings_get(const struct Fdt *fdt, u32 nameoff);

// Compare a NUL-terminated c-string against bytes from `[base..base+max)`.
// Returns true iff the bytes match s and the next byte (within max) is NUL.
static inline bool fdt_str_eq_bounded(const u8 *base, u32 max, const char *s)
{
    u32 i = 0;
    while (s[i] != '\0') {
        if (i >= max || base[i] != (u8)s[i]) {
            return false;
        }
        i++;
    }
    return i < max && base[i] == 0;
}

#endif
