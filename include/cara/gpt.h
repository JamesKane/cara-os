// SPDX-License-Identifier: BSD-2-Clause
//
// GUID Partition Table — discovery + formatting (Phase 2 Subgoal 3,
// docs/LOGAIC_BOOT.md). Cleanroom from the UEFI specification (no
// third-party code, PRINCIPLES §2): protective MBR at LBA 0, primary
// GPT header at LBA 1, a 128-entry partition array, and backup copies
// at the end of the disk. CRCs are IEEE 802.3 CRC-32 (reflected, poly
// 0xEDB88320) — distinct from CaraFS's CRC-32C.
//
// Allocation-free and portable behind the GptDev block seam: host unit
// tests bind it to memory, the kernel binds it to NVMe. On-disk
// integers are little-endian (every GPT-bearing host CaraOS targets is
// LE; the same gate as carafs.h).

#ifndef CARA_GPT_H
#define CARA_GPT_H

#include <cara/types.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "GPT implementation requires a little-endian host"
#endif

// Standard GPT geometry. A 128 × 128 B entry array (16 KiB) is the
// universal default every tool expects.
constexpr u64 GPT_HEADER_LBA = 1;
constexpr u64 GPT_ENTRY_LBA = 2;
constexpr u32 GPT_ENTRY_COUNT = 128;
constexpr u32 GPT_ENTRY_SIZE = 128;
constexpr u32 GPT_ARRAY_BYTES = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE; // 16384
constexpr u32 GPT_HEADER_SIZE = 92;
constexpr u32 GPT_REVISION = 0x00010000u;

// Partition starts are 1 MiB-aligned (flash erase-block / 4 Kn
// friendly), the universal convention.
constexpr u64 GPT_ALIGN_BYTES = 1u << 20;

// GPT magic: "EFI PART" at offset 0 of the header.
constexpr u8 GPT_SIGNATURE[8] = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };

// CaraFS partition type GUID (minted for this project, stable). Stored
// as raw bytes; comparison and emission both use this exact pattern, so
// the textual/mixed-endian representation is immaterial internally
// (docs/LOGAIC_BOOT.md §2).
constexpr u8 CARAFS_GPT_TYPE_GUID[16] = { 0x50, 0x0F, 0x1A, 0xCA, 0x00, 0x00, 0x61, 0x43,
                                          0x72, 0x61, 0x46, 0x53, 0x21, 0x00, 0x00, 0x00 };

// ---- On-disk structures (exact images) --------------------------------------

struct GptHeader {
    u8 signature[8]; // GPT_SIGNATURE
    u32 revision;
    u32 header_size; // GPT_HEADER_SIZE
    u32 header_crc32;
    u32 reserved;
    u64 my_lba;
    u64 alternate_lba;
    u64 first_usable_lba;
    u64 last_usable_lba;
    u8 disk_guid[16];
    u64 entry_lba;
    u32 entry_count;
    u32 entry_size;
    u32 entry_array_crc32;
    // padding to the end of the LBA, zeroed
};
static_assert(offsetof(struct GptHeader, header_crc32) == 16);
static_assert(offsetof(struct GptHeader, my_lba) == 24);
static_assert(offsetof(struct GptHeader, disk_guid) == 56);
static_assert(offsetof(struct GptHeader, entry_lba) == 72);
static_assert(offsetof(struct GptHeader, entry_array_crc32) == 88);
// The on-disk header is GPT_HEADER_SIZE (92) bytes; the C struct pads to
// 8 (u64 members), so checksums use GPT_HEADER_SIZE, never sizeof.
static_assert(sizeof(struct GptHeader) >= GPT_HEADER_SIZE);
static_assert(offsetof(struct GptHeader, entry_array_crc32) + 4 == GPT_HEADER_SIZE);

struct GptEntry {
    u8 type_guid[16];
    u8 unique_guid[16];
    u64 starting_lba;
    u64 ending_lba; // inclusive
    u64 attributes;
    u8 name[72]; // UTF-16LE
};
static_assert(sizeof(struct GptEntry) == GPT_ENTRY_SIZE);

// ---- Block device seam ------------------------------------------------------

struct GptDev {
    void *ctx;
    u32 lba_size;
    u64 n_lbas;
    int (*read)(void *ctx, u64 lba, u32 n, void *buf);
    int (*write)(void *ctx, u64 lba, u32 n, const void *buf);
};

// IEEE 802.3 CRC-32 (seed 0 for standalone; chain by passing the prior
// result). Used for GPT header + entry-array checksums.
[[nodiscard]] u32 Gpt_Crc32(u32 seed, const void *data, usize len);

// Locate the (first) CaraFS partition. Validates the primary GPT header
// and entry-array CRCs, then scans for a CARAFS_GPT_TYPE_GUID entry.
// `scratch` must hold >= lba_size + GPT_ARRAY_BYTES bytes. Fills
// *first_lba / *lba_count. Returns CARA_EOK on success, CARA_ENOENT when
// there is no valid GPT or no CaraFS partition, CARA_EINVAL on bad args,
// or the dev's error code on I/O failure.
[[nodiscard]] int Gpt_FindCarafs(struct GptDev *dev, void *scratch, usize scratch_bytes,
                                 u64 *first_lba_out, u64 *lba_count_out);

// As Gpt_FindCarafs but returns the `nth` (0-based) CaraFS partition in
// entry order; CARA_ENOENT once there are no more.
[[nodiscard]] int Gpt_FindCarafsNth(struct GptDev *dev, void *scratch, usize scratch_bytes, u32 nth,
                                    u64 *first_lba_out, u64 *lba_count_out);

// UUID-aware root selection (docs/LOGAIC_BOOT.md §1/§3): scan the CaraFS
// partitions and return the one whose volume superblock (read from the
// partition's first LBA) starts with `magic` (magic_len bytes at offset
// 0) and carries `target_uuid` (16 bytes at byte offset `uuid_off`).
// Generic so the GPT layer needn't know CaraFS's superblock shape — the
// caller passes CARAFS_SB_MAGIC and the uuid field offset. CARA_ENOENT
// when no partition matches.
[[nodiscard]] int Gpt_FindByVolumeUuid(struct GptDev *dev, void *scratch, usize scratch_bytes,
                                       const void *magic, u32 magic_len, u32 uuid_off,
                                       const u8 target_uuid[16], u64 *first_lba_out,
                                       u64 *lba_count_out);

// One partition for Gpt_FormatN. `last_lba` is inclusive.
struct GptPartSpec {
    u8 type_guid[16];
    u8 unique_guid[16];
    u64 first_lba;
    u64 last_lba;
};

// Lay a fresh GPT with `n_parts` caller-specified partitions (the
// general form of Gpt_Format). Each spec's range must lie within the
// usable region; the caller computes/aligns them. `scratch` as above.
[[nodiscard]] int Gpt_FormatN(struct GptDev *dev, const u8 disk_guid[16],
                              const struct GptPartSpec *specs, u32 n_parts, void *scratch,
                              usize scratch_bytes);

// First usable LBA after the primary header + entry array, and the last
// usable LBA before the backup array + header, for this device geometry
// — callers building GptPartSpec ranges need these bounds.
[[nodiscard]] u64 Gpt_FirstUsableLba(const struct GptDev *dev);
[[nodiscard]] u64 Gpt_LastUsableLba(const struct GptDev *dev);

// Lay a fresh GPT (protective MBR + primary/backup headers + entry
// arrays) with a single CaraFS partition spanning the 1 MiB-aligned
// usable region. `disk_guid` / `part_guid` are 16 bytes each (the core
// has no RNG; the caller supplies them). `scratch` as for FindCarafs.
// Returns the new partition range via *first_lba / *lba_count.
[[nodiscard]] int Gpt_Format(struct GptDev *dev, const u8 disk_guid[16], const u8 part_guid[16],
                             void *scratch, usize scratch_bytes, u64 *first_lba_out,
                             u64 *lba_count_out);

#endif
