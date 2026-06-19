// SPDX-License-Identifier: BSD-2-Clause
//
// GPT (Phase 2 Subgoal 3): format → discover round trip on a memory
// device, CaraFS-partition location + 1 MiB alignment, CRC validation
// (a corrupt header or entry array makes discovery fail), and the
// CRC-32 against a known vector.

#include <cara/gpt.h>
#include <cara/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

// ---- Memory GptDev ----------------------------------------------------------

struct MemDev {
    u8 *mem;
    u32 lba_size;
    u64 n_lbas;
};

static int mem_read(void *ctx, u64 lba, u32 n, void *buf)
{
    struct MemDev *d = ctx;
    if (lba + n > d->n_lbas) {
        return CARA_ERANGE;
    }
    memcpy(buf, d->mem + lba * d->lba_size, (size_t)n * d->lba_size);
    return CARA_EOK;
}

static int mem_write(void *ctx, u64 lba, u32 n, const void *buf)
{
    struct MemDev *d = ctx;
    if (lba + n > d->n_lbas) {
        return CARA_ERANGE;
    }
    memcpy(d->mem + lba * d->lba_size, buf, (size_t)n * d->lba_size);
    return CARA_EOK;
}

static void make_dev(struct GptDev *dev, struct MemDev *st, u8 *mem, u32 lba_size, u64 n_lbas)
{
    *st = (struct MemDev){ .mem = mem, .lba_size = lba_size, .n_lbas = n_lbas };
    *dev = (struct GptDev){
        .ctx = st, .lba_size = lba_size, .n_lbas = n_lbas, .read = mem_read, .write = mem_write
    };
}

static const u8 DISK_GUID[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const u8 PART_GUID[16] = { 0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5, 0x96, 0x87,
                                  0x78, 0x69, 0x5A, 0x4B, 0x3C, 0x2D, 0x1E, 0x0F };

// Round trip at a given LBA size: format, then discover the CaraFS
// partition; check alignment and that it lands inside the disk.
static void test_round_trip(u32 lba_size)
{
    u64 n_lbas = (64ull << 20) / lba_size; // 64 MiB disk
    u8 *mem = calloc(1, n_lbas * lba_size);
    u8 *scratch = malloc(lba_size + GPT_ARRAY_BYTES);

    struct MemDev st;
    struct GptDev dev;
    make_dev(&dev, &st, mem, lba_size, n_lbas);

    u64 first = 0, count = 0;
    CHECK(Gpt_Format(&dev, DISK_GUID, PART_GUID, scratch, lba_size + GPT_ARRAY_BYTES, &first,
                     &count) == CARA_EOK,
          "format");

    u64 f2 = 0, c2 = 0;
    CHECK(Gpt_FindCarafs(&dev, scratch, lba_size + GPT_ARRAY_BYTES, &f2, &c2) == CARA_EOK,
          "discover after format");
    CHECK(f2 == first && c2 == count, "discover matches format");
    CHECK((first * lba_size) % GPT_ALIGN_BYTES == 0, "partition 1 MiB aligned");
    CHECK(first + count <= n_lbas, "partition within disk");
    CHECK(count * lba_size > (48ull << 20), "partition spans most of the disk");

    // Protective MBR signature + 0xEE type.
    CHECK(mem[510] == 0x55 && mem[511] == 0xAA, "MBR boot signature");
    CHECK(mem[446 + 4] == 0xEE, "protective MBR partition type");

    // Backup header is present and self-consistent (alternate of the
    // primary). FindCarafs validates the primary; spot-check the backup
    // signature.
    const u8 *bak = mem + (n_lbas - 1) * lba_size;
    CHECK(memcmp(bak, GPT_SIGNATURE, 8) == 0, "backup header signature");

    free(mem);
    free(scratch);
}

// A corrupt primary header CRC or entry-array CRC must fail discovery.
static void test_crc_rejects(void)
{
    u32 lba_size = 512;
    u64 n_lbas = (32ull << 20) / lba_size;
    u8 *mem = calloc(1, n_lbas * lba_size);
    u8 *scratch = malloc(lba_size + GPT_ARRAY_BYTES);
    struct MemDev st;
    struct GptDev dev;
    make_dev(&dev, &st, mem, lba_size, n_lbas);

    u64 first, count;
    CHECK(Gpt_Format(&dev, DISK_GUID, PART_GUID, scratch, lba_size + GPT_ARRAY_BYTES, &first,
                     &count) == CARA_EOK,
          "format for crc test");
    CHECK(Gpt_FindCarafs(&dev, scratch, lba_size + GPT_ARRAY_BYTES, &first, &count) == CARA_EOK,
          "discover clean");

    // Flip a byte in the header LBA → CRC fails → no GPT found.
    mem[GPT_HEADER_LBA * lba_size + 40] ^= 0xFF;
    CHECK(Gpt_FindCarafs(&dev, scratch, lba_size + GPT_ARRAY_BYTES, &first, &count) == CARA_ENOENT,
          "corrupt header rejected");
    mem[GPT_HEADER_LBA * lba_size + 40] ^= 0xFF; // restore

    // Flip a byte in the entry array → array CRC fails.
    mem[GPT_ENTRY_LBA * lba_size + 70] ^= 0xFF;
    CHECK(Gpt_FindCarafs(&dev, scratch, lba_size + GPT_ARRAY_BYTES, &first, &count) == CARA_ENOENT,
          "corrupt entry array rejected");

    free(mem);
    free(scratch);
}

// No GPT at all → ENOENT (the kernel uses this to decide to format).
static void test_blank_disk(void)
{
    u32 lba_size = 512;
    u64 n_lbas = (8ull << 20) / lba_size;
    u8 *mem = calloc(1, n_lbas * lba_size);
    u8 *scratch = malloc(lba_size + GPT_ARRAY_BYTES);
    struct MemDev st;
    struct GptDev dev;
    make_dev(&dev, &st, mem, lba_size, n_lbas);

    u64 first, count;
    CHECK(Gpt_FindCarafs(&dev, scratch, lba_size + GPT_ARRAY_BYTES, &first, &count) == CARA_ENOENT,
          "blank disk has no GPT");

    free(mem);
    free(scratch);
}

// CRC-32 (IEEE) against the canonical "123456789" check value 0xCBF43926.
static void test_crc32_vector(void)
{
    CHECK(Gpt_Crc32(0, "123456789", 9) == 0xCBF43926u, "CRC-32 check vector");
}

int main(void)
{
    test_crc32_vector();
    test_round_trip(512);
    test_round_trip(4096);
    test_crc_rejects();
    test_blank_disk();

    if (failures) {
        printf("test_gpt: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_gpt: ok\n");
    return 0;
}
