// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS format helpers (epic F0, docs/CARAFS.md §3.1/§3.5):
// CRC-32C, the name fold/hash/compare trio, and name validation.
// Pure logic — builds identically for host and kernel.

#include <cara/carafs.h>
#include <cara/types.h>

// ---- CRC-32C (Castagnoli) ---------------------------------------------------
//
// Reflected polynomial 0x82F63B78 (forward 0x1EDC6F41), standard
// byte-at-a-time table driver. The 256-entry table is built lazily on
// first use — cheap, idempotent, and keeps the image free of a 1 KiB
// constant table. Written from the polynomial definition; reference
// check vector: CRC-32C("123456789") = 0xE3069283 (test_carafs_format).

static u32 crc_table[256];
static bool crc_table_ready;

static void crc_build_table(void)
{
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (u32 k = 0; k < 8; k++) {
            c = (c & 1u) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
        }
        crc_table[i] = c;
    }
    crc_table_ready = true;
}

[[nodiscard]] u32 Carafs_Crc32c(u32 seed, const void *data, usize len)
{
    if (!crc_table_ready) {
        crc_build_table();
    }
    const u8 *p = data;
    u32 c = ~seed;
    while (len--) {
        c = crc_table[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    }
    return ~c;
}

[[nodiscard]] u32 Carafs_BlockCrc(const void *block, u32 block_size, u32 crc_off)
{
    // CRC over [0, crc_off), then 4 zero bytes in place of the crc
    // field, then [crc_off+4, block_size). Chaining trick: the public
    // seed is the *output* of the previous stage (Carafs_Crc32c
    // re-inverts internally), so staged calls compose exactly.
    static const u8 zeros[4] = { 0 };
    const u8 *p = block;
    u32 c = Carafs_Crc32c(0, p, crc_off);
    c = Carafs_Crc32c(c, zeros, 4);
    c = Carafs_Crc32c(c, p + crc_off + 4, block_size - crc_off - 4);
    return c;
}

// ---- Names (§3.5) -----------------------------------------------------------

[[nodiscard]] u8 Carafs_FoldByte(u8 c)
{
    // ASCII-only fold; deliberately not FFS INTL's Latin-1 fold,
    // which would corrupt UTF-8 multibyte sequences (CARAFS.md §3.5).
    return (c >= 'A' && c <= 'Z') ? (u8)(c + ('a' - 'A')) : c;
}

[[nodiscard]] u64 Carafs_NameHash(const void *name, u32 len)
{
    // FNV-1a 64 over folded bytes.
    const u8 *p = name;
    u64 h = 14695981039346656037ull; // FNV offset basis
    for (u32 i = 0; i < len; i++) {
        h ^= Carafs_FoldByte(p[i]);
        h *= 1099511628211ull; // FNV prime
    }
    return h;
}

[[nodiscard]] bool Carafs_NameEq(const void *a, u32 a_len, const void *b, u32 b_len)
{
    if (a_len != b_len) {
        return false;
    }
    const u8 *pa = a;
    const u8 *pb = b;
    for (u32 i = 0; i < a_len; i++) {
        if (Carafs_FoldByte(pa[i]) != Carafs_FoldByte(pb[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool Carafs_NameValid(const void *name, u32 len)
{
    if (len < 1 || len > CARAFS_NAME_MAX) {
        return false;
    }
    const u8 *p = name;
    for (u32 i = 0; i < len; i++) {
        if (p[i] == '/' || p[i] == ':' || p[i] == '\0') {
            return false;
        }
    }
    return true;
}
