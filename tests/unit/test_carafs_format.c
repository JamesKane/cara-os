// SPDX-License-Identifier: BSD-2-Clause
//
// CaraFS F0: format-layer unit tests. The struct offsets are pinned
// at compile time by the static_asserts in <cara/carafs.h>; here we
// verify the runtime helpers against independent golden vectors:
// CRC-32C's published check value, FNV-1a 64's published basis
// vectors, and the fold/validate rules of CARAFS.md §3.5.

#include <cara/carafs.h>
#include <cara/types.h>

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void test_crc32c(void)
{
    // The canonical CRC-32C check vector (RFC 3720 appendix / every
    // published implementation): "123456789" → 0xE3069283.
    CHECK(Carafs_Crc32c(0, "123456789", 9) == 0xE3069283u, "CRC-32C check vector");
    CHECK(Carafs_Crc32c(0, "", 0) == 0, "CRC-32C of empty input is 0");

    // Chaining: crc(a+b) == crc staged over a then b.
    u32 whole = Carafs_Crc32c(0, "123456789", 9);
    u32 staged = Carafs_Crc32c(0, "1234", 4);
    staged = Carafs_Crc32c(staged, "56789", 5);
    CHECK(whole == staged, "CRC-32C chaining composes");

    // 32 zero bytes — a second fixed vector so a table-generation bug
    // can't hide behind the check string. CRC-32C(32 x 0x00) =
    // 0x8A9136AA (published iSCSI test pattern).
    u8 zeros[32] = { 0 };
    CHECK(Carafs_Crc32c(0, zeros, 32) == 0x8A9136AAu, "CRC-32C 32-zeros vector");
}

static void test_block_crc(void)
{
    // BlockCrc must equal a straight CRC over a copy with the crc
    // field zeroed — and must not depend on what the field holds.
    u8 block[512];
    for (u32 i = 0; i < sizeof(block); i++) {
        block[i] = (u8)(i * 31u + 7u);
    }
    u32 crc_off = 8; // e.g. superblock crc at offset 8

    u8 copy[512];
    memcpy(copy, block, sizeof(block));
    memset(copy + crc_off, 0, 4);
    u32 want = Carafs_Crc32c(0, copy, sizeof(copy));

    CHECK(Carafs_BlockCrc(block, sizeof(block), crc_off) == want,
          "BlockCrc == CRC of field-zeroed copy");

    block[crc_off] ^= 0xFF; // stored crc value must be irrelevant
    CHECK(Carafs_BlockCrc(block, sizeof(block), crc_off) == want,
          "BlockCrc ignores the stored crc value");

    block[100] ^= 0x01; // any other bit must matter
    CHECK(Carafs_BlockCrc(block, sizeof(block), crc_off) != want, "BlockCrc detects a flipped bit");
}

static void test_fold_and_hash(void)
{
    CHECK(Carafs_FoldByte('A') == 'a' && Carafs_FoldByte('Z') == 'z', "ASCII fold");
    CHECK(Carafs_FoldByte('a') == 'a' && Carafs_FoldByte('0') == '0', "non-uppercase untouched");
    CHECK(Carafs_FoldByte(0xC3) == 0xC3 && Carafs_FoldByte(0xA9) == 0xA9,
          "UTF-8 multibyte bytes never folded");

    // FNV-1a 64 published vectors (no fold effect on these inputs).
    CHECK(Carafs_NameHash("", 0) == 14695981039346656037ull, "FNV-1a 64 offset basis");
    CHECK(Carafs_NameHash("a", 1) == 0xAF63DC4C8601EC8Cull, "FNV-1a 64 of 'a'");

    // Case-insensitivity comes from the fold.
    CHECK(Carafs_NameHash("FooBar", 6) == Carafs_NameHash("foobar", 6), "hash is case-insensitive");
    CHECK(Carafs_NameHash("foo", 3) != Carafs_NameHash("bar", 3), "different names differ");

    CHECK(Carafs_NameEq("FooBar", 6, "fOObAR", 6), "NameEq folds");
    CHECK(!Carafs_NameEq("foo", 3, "foo2", 4), "NameEq length mismatch");
    // Latin-1 capital À (0xC3 0x80 in UTF-8) must NOT equal à
    // (0xC3 0xA0): ASCII-only fold, by design.
    CHECK(!Carafs_NameEq("\xC3\x80", 2, "\xC3\xA0", 2), "no Latin-1 folding (UTF-8 safety)");
}

static void test_name_valid(void)
{
    CHECK(Carafs_NameValid("a", 1), "minimal name");
    CHECK(Carafs_NameValid("Work.Disk", 9), "dots fine");
    CHECK(!Carafs_NameValid("", 0), "empty rejected");
    CHECK(!Carafs_NameValid("a/b", 3), "slash rejected");
    CHECK(!Carafs_NameValid("Work:", 5), "colon rejected");
    CHECK(!Carafs_NameValid("a\0b", 3), "NUL rejected");

    char long_name[256];
    memset(long_name, 'x', sizeof(long_name));
    CHECK(Carafs_NameValid(long_name, 255), "255 bytes ok");
    CHECK(!Carafs_NameValid(long_name, 256), "256 bytes rejected");
}

static void test_epoch_constants(void)
{
    // 1978-01-01 minus 1970-01-01 = 2922 days (two leap years: 1972,
    // 1976) = 252,460,800 s. A DateStamp tick is exactly 20 ms.
    CHECK(CARAFS_EPOCH_UNIX_OFFSET_S == 2922ull * 86400ull, "Amiga epoch offset");
    CHECK(CARAFS_NS_PER_TICK * 50 == 1000000000ull, "50 ticks per second");
}

int main(void)
{
    test_crc32c();
    test_block_crc();
    test_fold_and_hash();
    test_name_valid();
    test_epoch_constants();

    if (failures) {
        printf("test_carafs_format: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_carafs_format: ok\n");
    return 0;
}
