// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel CaraFS ↔ NVMe binding (F5; CARAFS.md §4, docs/PHASE2_NVME.md).
//
// The CaraFS core is portable pure logic behind the CarafsBdev seam
// (read/write/flush + block geometry). On the host the seam binds to a
// file; here it binds to the NVMe driver. One filesystem block maps to
// block_size/LBA-size contiguous LBAs on NSID 1.
//
// Two impedance mismatches to bridge:
//   - DMA alignment. Croi_Nvme_Read/Write require a page-aligned
//     direct-map buffer of <= 8 KiB (PRP1 + PRP2, no PRP list). The
//     CaraFS cache frames are only 64-byte aligned, so every transfer
//     bounces through a page-aligned 2-page buffer, in <= 8 KiB chunks.
//   - Durability. bdev->flush issues an NVM Flush so the journal's
//     ordering/commit barriers hold on real media.
//
// Single-mounter by contract (CaraFS §4), so one bounce buffer and one
// global mount suffice; the Phase 3 dos.library handler will own this.

#include <cara/carafs.h>
#include <cara/carafs_bind.h>
#include <cara/gpt.h>
#include <cara/log.h>
#include <cara/mm.h>
#include <cara/nvme.h>
#include <cara/types.h>

// Freestanding kernel build: the compiler emits memcpy calls; the
// kernel's string.c provides the symbol (same pattern as carafs/internal.h).
#if __has_include(<string.h>)
#include <string.h>
#else
void *memcpy(void *dst, const void *src, size_t n);
#endif

extern struct NvmeController g_nvme;
extern struct PageAllocator g_page_alloc;

struct CarafsMount g_carafs;
bool g_carafs_mounted = false;

// Filesystem block size for kernel volumes: the 4 KiB default (matches
// the page allocator and the NVMe PRP path).
static constexpr u32 KFS_BLOCK_SIZE = 1u << CARAFS_DEF_BLOCK_LOG2;
static constexpr u32 BOUNCE_PAGES = 2; // 8 KiB == one NVMe PRP1+PRP2 transfer

struct CroiCarafsCtx {
    struct NvmeController *nvme;
    u32 block_bytes;   // NVMe LBA size
    u32 lba_per_block; // KFS_BLOCK_SIZE / block_bytes
    u64 part_base_lba; // first LBA of the CaraFS partition (GPT, §3)
    u8 *bounce;        // BOUNCE_PAGES, page-aligned (DMA-capable)
};

static struct CroiCarafsCtx g_ctx;
static struct CarafsBdev g_carafs_bdev;

// 256 KiB cache arena (~60 frames at 4 KiB after overhead) — no DMA, so
// plain BSS; page-aligned to satisfy the cache's internal alignment.
alignas(CARA_PAGE_SIZE) static u8 g_carafs_arena[256 * 1024];

// One filesystem block of scratch for mkfs. Distinct from the bounce
// buffer (mkfs writes route through bdev->write, which uses the bounce).
alignas(8) static u8 g_mkfs_scratch[KFS_BLOCK_SIZE];

// Transfer `n_lbas` absolute LBAs to/from NSID 1, bouncing in <= 8 KiB
// chunks (one NVMe PRP1+PRP2) through the page-aligned DMA buffer —
// Croi_Nvme_Read/Write need a page-aligned <= 2-page buffer, the cache
// frames are only 64-byte aligned. Shared by the GPT (raw-LBA) and
// CaraFS (FS-block) paths.
static int nvme_chunked(struct CroiCarafsCtx *c, u64 lba, u32 n_lbas, void *buf, bool write)
{
    u64 total = (u64)n_lbas * c->block_bytes;
    u64 off = 0;
    while (off < total) {
        u32 chunk = (u32)(total - off);
        if (chunk > BOUNCE_PAGES * CARA_PAGE_SIZE) {
            chunk = BOUNCE_PAGES * CARA_PAGE_SIZE;
        }
        u64 clba = lba + off / c->block_bytes;
        u32 cn = chunk / c->block_bytes;
        int rc;
        if (write) {
            memcpy(c->bounce, (const u8 *)buf + off, chunk);
            rc = Croi_Nvme_Write(c->nvme, clba, cn, c->bounce);
        } else {
            rc = Croi_Nvme_Read(c->nvme, clba, cn, c->bounce);
            if (rc == CARA_EOK) {
                memcpy((u8 *)buf + off, c->bounce, chunk);
            }
        }
        if (rc != CARA_EOK) {
            return rc;
        }
        off += chunk;
    }
    return CARA_EOK;
}

// CaraFS bdev: filesystem block `block` → partition-relative LBA range.
static int bd_read(void *ctx, u64 block, u32 n, void *buf)
{
    struct CroiCarafsCtx *c = ctx;
    return nvme_chunked(c, c->part_base_lba + block * c->lba_per_block, n * c->lba_per_block, buf,
                        false);
}

static int bd_write(void *ctx, u64 block, u32 n, const void *buf)
{
    struct CroiCarafsCtx *c = ctx;
    return nvme_chunked(c, c->part_base_lba + block * c->lba_per_block, n * c->lba_per_block,
                        (void *)buf, true);
}

static int bd_flush(void *ctx)
{
    struct CroiCarafsCtx *c = ctx;
    return Croi_Nvme_Flush(c->nvme);
}

// GPT dev: absolute-LBA access (no partition offset, no FS-block scale).
static int gpt_read(void *ctx, u64 lba, u32 n, void *buf)
{
    return nvme_chunked(ctx, lba, n, buf, false);
}

static int gpt_write(void *ctx, u64 lba, u32 n, const void *buf)
{
    return nvme_chunked(ctx, lba, n, (void *)buf, true);
}

[[nodiscard]] int Croi_Carafs_BringUp(void)
{
    if (g_carafs_mounted) {
        return CARA_EOK;
    }
    if (!g_nvme.io_ready || !g_nvme.ns.valid) {
        return CARA_EINVAL;
    }
    u32 bb = g_nvme.ns.block_bytes;
    if (bb == 0 || KFS_BLOCK_SIZE % bb != 0) {
        LOG_ERROR("carafs", "LBA size %u does not divide the %u FS block", (unsigned)bb,
                  (unsigned)KFS_BLOCK_SIZE);
        return CARA_EINVAL;
    }

    // One DMA bounce buffer, page-aligned via the page allocator.
    u64 bounce_phys = Page_Alloc(&g_page_alloc, BOUNCE_PAGES);
    if (bounce_phys == 0) {
        return CARA_ENOMEM;
    }
    g_ctx = (struct CroiCarafsCtx){
        .nvme = &g_nvme,
        .block_bytes = bb,
        .lba_per_block = KFS_BLOCK_SIZE / bb,
        .bounce = (u8 *)Mm_PhysToVirt(bounce_phys),
    };

    // ---- GPT (docs/LOGAIC_BOOT.md §3): find the CaraFS partition, or
    //      partition the blank namespace ourselves (symmetric with the
    //      mkfs-on-empty below). GPT scratch needs one LBA + the 16 KiB
    //      entry array.
    u32 gpt_pages = (u32)((bb + GPT_ARRAY_BYTES + CARA_PAGE_SIZE - 1) / CARA_PAGE_SIZE);
    u64 gpt_scratch_phys = Page_Alloc(&g_page_alloc, gpt_pages);
    if (gpt_scratch_phys == 0) {
        Page_Free(&g_page_alloc, bounce_phys, BOUNCE_PAGES);
        return CARA_ENOMEM;
    }
    u8 *gpt_scratch = (u8 *)Mm_PhysToVirt(gpt_scratch_phys);
    usize gpt_scratch_bytes = (usize)gpt_pages * CARA_PAGE_SIZE;
    struct GptDev gdev = {
        .ctx = &g_ctx,
        .lba_size = bb,
        .n_lbas = g_nvme.ns.n_blocks,
        .read = gpt_read,
        .write = gpt_write,
    };
    u64 part_first = 0;
    u64 part_lbas = 0;
    int grc = Gpt_FindCarafs(&gdev, gpt_scratch, gpt_scratch_bytes, &part_first, &part_lbas);
    if (grc == CARA_ENOENT) {
        LOG_INFO("carafs", "no GPT; partitioning the namespace");
        // No RNG in the boot path; derive stable GUIDs from a constant.
        static const u8 disk_guid[16] = { 0xCA, 0x1A, 0x05, 0x00, 0xD1, 0x5C, 0x00, 0x01,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 };
        static const u8 part_guid[16] = { 0xCA, 0x1A, 0x05, 0x00, 0x9A, 0x27, 0x00, 0x01,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 };
        grc = Gpt_Format(&gdev, disk_guid, part_guid, gpt_scratch, gpt_scratch_bytes, &part_first,
                         &part_lbas);
    }
    Page_Free(&g_page_alloc, gpt_scratch_phys, gpt_pages);
    if (grc != CARA_EOK) {
        LOG_ERROR("carafs", "GPT discovery/format failed: %d", grc);
        Page_Free(&g_page_alloc, bounce_phys, BOUNCE_PAGES);
        return grc;
    }
    g_ctx.part_base_lba = part_first;

    u64 fs_blocks = part_lbas / g_ctx.lba_per_block;
    g_carafs_bdev = (struct CarafsBdev){
        .ctx = &g_ctx,
        .block_size = KFS_BLOCK_SIZE,
        .n_blocks = fs_blocks,
        .read = bd_read,
        .write = bd_write,
        .flush = bd_flush,
    };

    struct CarafsMountOpts mopts = {
        .cache_mem = g_carafs_arena,
        .cache_bytes = sizeof(g_carafs_arena),
    };
    int rc = Carafs_Mount(&g_carafs, &g_carafs_bdev, &mopts);
    if (rc == CARA_EBADMAGIC || rc == CARA_EBADVERSION) {
        // No (recognised) CaraFS volume here — format one and retry.
        LOG_INFO("carafs", "no volume found (%d); formatting %llu blocks", rc,
                 (unsigned long long)fs_blocks);
        struct CarafsMkfsOpts fopts = {
            .block_size_log2 = CARAFS_DEF_BLOCK_LOG2,
            .name = "Work",
            .name_len = 4,
        };
        rc = Carafs_Mkfs(&g_carafs_bdev, &fopts, g_mkfs_scratch, sizeof(g_mkfs_scratch));
        if (rc != CARA_EOK) {
            LOG_ERROR("carafs", "mkfs failed: %d", rc);
            return rc;
        }
        rc = Carafs_Mount(&g_carafs, &g_carafs_bdev, &mopts);
    }
    if (rc != CARA_EOK) {
        LOG_ERROR("carafs", "mount failed: %d", rc);
        return rc;
    }

    g_carafs_mounted = true;
    LOG_INFO("carafs", "mounted nvme partition @lba %llu: %llu blocks x %u B, %llu free",
             (unsigned long long)part_first, (unsigned long long)g_carafs.sb.total_blocks,
             (unsigned)KFS_BLOCK_SIZE, (unsigned long long)g_carafs.sb.free_blocks);
    return CARA_EOK;
}
