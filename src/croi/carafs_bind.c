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

// Move `n` filesystem blocks at `block` to/from NSID 1, bouncing in
// <= 8 KiB chunks through the page-aligned DMA buffer.
static int bd_rw(struct CroiCarafsCtx *c, u64 block, u32 n, void *buf, bool write)
{
    u64 total = (u64)n * KFS_BLOCK_SIZE;
    u64 off = 0;
    while (off < total) {
        u32 chunk = (u32)(total - off);
        if (chunk > BOUNCE_PAGES * CARA_PAGE_SIZE) {
            chunk = BOUNCE_PAGES * CARA_PAGE_SIZE;
        }
        u64 lba = block * c->lba_per_block + off / c->block_bytes;
        u32 nlba = chunk / c->block_bytes;
        int rc;
        if (write) {
            memcpy(c->bounce, (const u8 *)buf + off, chunk);
            rc = Croi_Nvme_Write(c->nvme, lba, nlba, c->bounce);
        } else {
            rc = Croi_Nvme_Read(c->nvme, lba, nlba, c->bounce);
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

static int bd_read(void *ctx, u64 block, u32 n, void *buf)
{
    return bd_rw(ctx, block, n, buf, false);
}

static int bd_write(void *ctx, u64 block, u32 n, const void *buf)
{
    return bd_rw(ctx, block, n, (void *)buf, true);
}

static int bd_flush(void *ctx)
{
    struct CroiCarafsCtx *c = ctx;
    return Croi_Nvme_Flush(c->nvme);
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

    u64 fs_blocks = g_nvme.ns.n_blocks / g_ctx.lba_per_block;
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
    LOG_INFO("carafs", "mounted on nvme: %llu blocks x %u B, %llu free",
             (unsigned long long)g_carafs.sb.total_blocks, (unsigned)KFS_BLOCK_SIZE,
             (unsigned long long)g_carafs.sb.free_blocks);
    return CARA_EOK;
}
