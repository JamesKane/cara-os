// SPDX-License-Identifier: BSD-2-Clause
//
// GUID Partition Table discovery + formatting (Phase 2 Subgoal 3,
// docs/LOGAIC_BOOT.md). Cleanroom from the UEFI spec. Portable pure
// logic behind the GptDev seam; the kernel reads/lays the GPT on NVMe,
// host tests drive a memory device.
//
// Partitions are 1 MiB-aligned (GPT_ALIGN_BYTES) — the universal
// convention for flash erase blocks and 4 Kn drives — even though
// QEMU's nvme presents 512e geometry.

#include <cara/gpt.h>
#include <cara/types.h>

#if __has_include(<string.h>)
#include <string.h>
#else
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
#endif

// ---- CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) ------------------------

[[nodiscard]] u32 Gpt_Crc32(u32 seed, const void *data, usize len)
{
    u32 crc = ~seed;
    const u8 *p = data;
    for (usize i = 0; i < len; i++) {
        crc ^= p[i];
        for (u32 b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
        }
    }
    return ~crc;
}

// ---- Helpers ----------------------------------------------------------------

static u32 array_lba_count(u32 lba_size)
{
    return (GPT_ARRAY_BYTES + lba_size - 1) / lba_size;
}

// Header CRC over the first GPT_HEADER_SIZE bytes with the crc field
// zeroed; restores it afterwards. Operates on the live block buffer.
static u32 header_crc(u8 *hdrblk)
{
    struct GptHeader *h = (struct GptHeader *)hdrblk;
    u32 saved = h->header_crc32;
    h->header_crc32 = 0;
    u32 crc = Gpt_Crc32(0, hdrblk, GPT_HEADER_SIZE);
    h->header_crc32 = saved;
    return crc;
}

[[nodiscard]] int Gpt_FindCarafsNth(struct GptDev *dev, void *scratch, usize scratch_bytes, u32 nth,
                                    u64 *first_lba_out, u64 *lba_count_out)
{
    if (!dev || !dev->read || !scratch || !first_lba_out || !lba_count_out) {
        return CARA_EINVAL;
    }
    if (scratch_bytes < (usize)dev->lba_size + GPT_ARRAY_BYTES) {
        return CARA_EINVAL;
    }
    u8 *hdrblk = scratch;
    u8 *array = (u8 *)scratch + dev->lba_size;

    int rc = dev->read(dev->ctx, GPT_HEADER_LBA, 1, hdrblk);
    if (rc != CARA_EOK) {
        return rc;
    }
    struct GptHeader *h = (struct GptHeader *)hdrblk;
    if (memcmp(h->signature, GPT_SIGNATURE, 8) != 0 || h->header_size < GPT_HEADER_SIZE ||
        h->header_size > dev->lba_size) {
        return CARA_ENOENT;
    }
    if (header_crc(hdrblk) != h->header_crc32) {
        return CARA_ENOENT;
    }
    u32 ecount = h->entry_count;
    u32 esize = h->entry_size;
    if (ecount == 0 || esize < sizeof(struct GptEntry) || (u64)ecount * esize > GPT_ARRAY_BYTES) {
        return CARA_ENOENT;
    }
    u64 abytes = (u64)ecount * esize;
    u32 albas = (u32)((abytes + dev->lba_size - 1) / dev->lba_size);
    rc = dev->read(dev->ctx, h->entry_lba, albas, array);
    if (rc != CARA_EOK) {
        return rc;
    }
    if (Gpt_Crc32(0, array, abytes) != h->entry_array_crc32) {
        return CARA_ENOENT;
    }

    u32 seen = 0;
    for (u32 i = 0; i < ecount; i++) {
        const struct GptEntry *e = (const struct GptEntry *)(array + (usize)i * esize);
        if (memcmp(e->type_guid, CARAFS_GPT_TYPE_GUID, 16) != 0) {
            continue;
        }
        if (e->starting_lba == 0 || e->ending_lba < e->starting_lba ||
            e->ending_lba >= dev->n_lbas) {
            continue; // malformed entry
        }
        if (seen++ != nth) {
            continue;
        }
        *first_lba_out = e->starting_lba;
        *lba_count_out = e->ending_lba - e->starting_lba + 1;
        return CARA_EOK;
    }
    return CARA_ENOENT;
}

[[nodiscard]] int Gpt_FindCarafs(struct GptDev *dev, void *scratch, usize scratch_bytes,
                                 u64 *first_lba_out, u64 *lba_count_out)
{
    return Gpt_FindCarafsNth(dev, scratch, scratch_bytes, 0, first_lba_out, lba_count_out);
}

[[nodiscard]] int Gpt_FindByVolumeUuid(struct GptDev *dev, void *scratch, usize scratch_bytes,
                                       const void *magic, u32 magic_len, u32 uuid_off,
                                       const u8 target_uuid[16], u64 *first_lba_out,
                                       u64 *lba_count_out)
{
    if (!dev || !dev->read || !magic || !target_uuid || uuid_off + 16 > dev->lba_size) {
        return CARA_EINVAL;
    }
    for (u32 nth = 0;; nth++) {
        u64 first, count;
        int rc = Gpt_FindCarafsNth(dev, scratch, scratch_bytes, nth, &first, &count);
        if (rc != CARA_EOK) {
            return rc; // ENOENT once the partitions are exhausted
        }
        // Read the volume's first LBA (holds the superblock) and match.
        // Reusing the header slot of scratch — the array is no longer
        // needed for this candidate.
        u8 *sb = scratch;
        if (dev->read(dev->ctx, first, 1, sb) != CARA_EOK) {
            continue;
        }
        if (memcmp(sb, magic, magic_len) == 0 && memcmp(sb + uuid_off, target_uuid, 16) == 0) {
            *first_lba_out = first;
            *lba_count_out = count;
            return CARA_EOK;
        }
    }
}

// Fill an already-zeroed block with a GPT header for the given role.
static void build_header(u8 *blk, u64 my_lba, u64 alt_lba, u64 entry_lba, u64 first_usable,
                         u64 last_usable, const u8 disk_guid[16], u32 entry_crc)
{
    struct GptHeader *h = (struct GptHeader *)blk;
    memcpy(h->signature, GPT_SIGNATURE, 8);
    h->revision = GPT_REVISION;
    h->header_size = GPT_HEADER_SIZE;
    h->my_lba = my_lba;
    h->alternate_lba = alt_lba;
    h->first_usable_lba = first_usable;
    h->last_usable_lba = last_usable;
    memcpy(h->disk_guid, disk_guid, 16);
    h->entry_lba = entry_lba;
    h->entry_count = GPT_ENTRY_COUNT;
    h->entry_size = GPT_ENTRY_SIZE;
    h->entry_array_crc32 = entry_crc;
    h->header_crc32 = 0;
    h->header_crc32 = Gpt_Crc32(0, blk, GPT_HEADER_SIZE);
}

[[nodiscard]] u64 Gpt_FirstUsableLba(const struct GptDev *dev)
{
    return GPT_ENTRY_LBA + array_lba_count(dev->lba_size);
}

[[nodiscard]] u64 Gpt_LastUsableLba(const struct GptDev *dev)
{
    return dev->n_lbas - 1 - array_lba_count(dev->lba_size) - 1;
}

[[nodiscard]] int Gpt_FormatN(struct GptDev *dev, const u8 disk_guid[16],
                              const struct GptPartSpec *specs, u32 n_parts, void *scratch,
                              usize scratch_bytes)
{
    if (!dev || !dev->read || !dev->write || !disk_guid || !specs || n_parts == 0 ||
        n_parts > GPT_ENTRY_COUNT || !scratch) {
        return CARA_EINVAL;
    }
    u32 ls = dev->lba_size;
    u64 n = dev->n_lbas;
    if (scratch_bytes < (usize)ls + GPT_ARRAY_BYTES || ls < 512 || GPT_ARRAY_BYTES % ls != 0) {
        return CARA_EINVAL;
    }
    u32 albas = array_lba_count(ls);
    u64 first_usable = GPT_ENTRY_LBA + albas;
    if (n < first_usable + albas + 2) {
        return CARA_ERANGE;
    }
    u64 last_usable = n - 1 - albas - 1;

    u8 *blk = scratch;
    u8 *array = (u8 *)scratch + ls;

    memset(array, 0, GPT_ARRAY_BYTES);
    static const u8 nm[] = { 'C', 0, 'a', 0, 'r', 0, 'a', 0, 'F', 0, 'S', 0 }; // UTF-16LE
    for (u32 i = 0; i < n_parts; i++) {
        const struct GptPartSpec *s = &specs[i];
        if (s->first_lba < first_usable || s->last_lba > last_usable ||
            s->last_lba < s->first_lba) {
            return CARA_ERANGE;
        }
        struct GptEntry *e = (struct GptEntry *)(array + (usize)i * GPT_ENTRY_SIZE);
        memcpy(e->type_guid, s->type_guid, 16);
        memcpy(e->unique_guid, s->unique_guid, 16);
        e->starting_lba = s->first_lba;
        e->ending_lba = s->last_lba;
        e->attributes = 0;
        memcpy(e->name, nm, sizeof(nm));
    }
    u32 acrc = Gpt_Crc32(0, array, GPT_ARRAY_BYTES);

    // Protective MBR (LBA 0): one 0xEE partition covering the disk.
    memset(blk, 0, ls);
    blk[446 + 4] = 0xEE;
    u32 mbr_first = 1;
    memcpy(blk + 446 + 8, &mbr_first, 4);
    u32 mbr_size = (n - 1 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (u32)(n - 1);
    memcpy(blk + 446 + 12, &mbr_size, 4);
    blk[510] = 0x55;
    blk[511] = 0xAA;
    int rc = dev->write(dev->ctx, 0, 1, blk);
    if (rc != CARA_EOK) {
        return rc;
    }

    // Primary header + array.
    memset(blk, 0, ls);
    build_header(blk, GPT_HEADER_LBA, n - 1, GPT_ENTRY_LBA, first_usable, last_usable, disk_guid,
                 acrc);
    rc = dev->write(dev->ctx, GPT_HEADER_LBA, 1, blk);
    if (rc != CARA_EOK) {
        return rc;
    }
    rc = dev->write(dev->ctx, GPT_ENTRY_LBA, albas, array);
    if (rc != CARA_EOK) {
        return rc;
    }

    // Backup array + header.
    u64 bak_array_lba = n - 1 - albas;
    rc = dev->write(dev->ctx, bak_array_lba, albas, array);
    if (rc != CARA_EOK) {
        return rc;
    }
    memset(blk, 0, ls);
    build_header(blk, n - 1, GPT_HEADER_LBA, bak_array_lba, first_usable, last_usable, disk_guid,
                 acrc);
    return dev->write(dev->ctx, n - 1, 1, blk);
}

[[nodiscard]] int Gpt_Format(struct GptDev *dev, const u8 disk_guid[16], const u8 part_guid[16],
                             void *scratch, usize scratch_bytes, u64 *first_lba_out,
                             u64 *lba_count_out)
{
    if (!dev || !part_guid || !first_lba_out || !lba_count_out || dev->lba_size < 512) {
        return CARA_EINVAL;
    }
    // One CaraFS partition spanning the 1 MiB-aligned usable region.
    u64 first_usable = Gpt_FirstUsableLba(dev);
    u64 last_usable = Gpt_LastUsableLba(dev);
    u64 align = GPT_ALIGN_BYTES / dev->lba_size;
    if (align == 0) {
        align = 1;
    }
    u64 part_first = (first_usable + align - 1) / align * align;
    if (part_first > last_usable) {
        return CARA_ERANGE;
    }
    struct GptPartSpec spec = { .first_lba = part_first, .last_lba = last_usable };
    memcpy(spec.type_guid, CARAFS_GPT_TYPE_GUID, 16);
    memcpy(spec.unique_guid, part_guid, 16);
    int rc = Gpt_FormatN(dev, disk_guid, &spec, 1, scratch, scratch_bytes);
    if (rc != CARA_EOK) {
        return rc;
    }
    *first_lba_out = part_first;
    *lba_count_out = last_usable - part_first + 1;
    return CARA_EOK;
}
