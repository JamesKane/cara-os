// SPDX-License-Identifier: BSD-2-Clause
//
// QEMU ramfb bring-up over fw-cfg MMIO (see ramfb.h). The flow:
//   1. find the "qemu,fw-cfg-mmio" node in the FDT -> register base,
//   2. walk the fw-cfg file directory to find the "etc/ramfb" selector,
//   3. allocate a contiguous framebuffer from the page allocator,
//   4. DMA-write a RAMFBCfg (big-endian) naming that buffer to etc/ramfb.
// All fw-cfg DMA structures are big-endian on the wire; RISC-V is
// little-endian, so every multi-byte field is byte-swapped.

#include "ramfb.h"

#include <cara/attr.h>
#include <cara/dath.h>
#include <cara/fdt.h>
#include <cara/log.h>
#include <cara/mm.h>
#include <cara/types.h>

// fw-cfg MMIO register offsets from the node's reg base.
#define FW_CFG_REG_DATA 0x00     // (unused here — DMA path only)
#define FW_CFG_REG_SELECTOR 0x08 // (unused here — DMA path only)
#define FW_CFG_REG_DMA_HI 0x10   // be32: high half of the DMA-access phys
#define FW_CFG_REG_DMA_LO 0x14   // be32: low half — writing it triggers

// fw-cfg selectors / DMA control bits.
#define FW_CFG_FILE_DIR 0x0019
#define FW_CFG_DMA_CTL_ERROR 0x01
#define FW_CFG_DMA_CTL_READ 0x02
#define FW_CFG_DMA_CTL_SKIP 0x04
#define FW_CFG_DMA_CTL_SELECT 0x08
#define FW_CFG_DMA_CTL_WRITE 0x10

// DRM fourcc for 32bpp little-endian 0x__RRGGBB — matches Dath's
// DATH_FMT_RGBA8888 memory layout (bytes B,G,R,x).
#define DRM_FORMAT_XRGB8888 0x34325258u // 'X','R','2','4'

struct CARA_PACKED FWCfgDmaAccess {
    u32 control; // big-endian
    u32 length;  // big-endian
    u64 address; // big-endian
};

struct CARA_PACKED RAMFBCfg {
    u64 addr;   // big-endian — framebuffer physical address
    u32 fourcc; // big-endian
    u32 flags;  // big-endian
    u32 width;  // big-endian
    u32 height; // big-endian
    u32 stride; // big-endian
};

// DMA-access scratch lives in kernel BSS; QEMU updates `.control` in
// place, so it's volatile and we read it back to poll for completion.
static volatile struct FWCfgDmaAccess g_dma CARA_ALIGNED(16);

// Run one fw-cfg DMA operation against `buf` (a kernel pointer) and
// block until QEMU clears the control word. `control` already carries
// the selector in its high 16 bits when FW_CFG_DMA_CTL_SELECT is set.
static int fw_cfg_dma(volatile u8 *regs, u32 control, u32 length, void *buf)
{
    g_dma.control = __builtin_bswap32(control);
    g_dma.length = __builtin_bswap32(length);
    g_dma.address = __builtin_bswap64(Mm_VirtToPhys(buf));

    u64 acc_phys = Mm_VirtToPhys((void *)&g_dma);
    __asm__ volatile("fence ow, ow" ::: "memory");
    *(volatile u32 *)(regs + FW_CFG_REG_DMA_HI) = __builtin_bswap32((u32)(acc_phys >> 32));
    *(volatile u32 *)(regs + FW_CFG_REG_DMA_LO) = __builtin_bswap32((u32)(acc_phys & 0xffffffffu));

    // Poll the control word; QEMU clears it to 0 on success, sets the
    // ERROR bit on failure. The op is synchronous under TCG, so the
    // bound is just a runaway guard.
    for (u64 spin = 0; spin < 100000000ull; spin++) {
        __asm__ volatile("fence ir, ir" ::: "memory");
        u32 c = __builtin_bswap32(g_dma.control);
        if (c == 0) {
            return CARA_EOK;
        }
        if (c & FW_CFG_DMA_CTL_ERROR) {
            return CARA_EAGAIN;
        }
    }
    return CARA_EAGAIN;
}

static bool name_is_etc_ramfb(const char name[56])
{
    static const char want[] = "etc/ramfb";
    for (u32 i = 0; i < sizeof(want); i++) { // includes the NUL
        if (name[i] != want[i]) {
            return false;
        }
    }
    return true;
}

// Find the etc/ramfb fw-cfg selector by walking the file directory.
// Returns CARA_EOK and *sel_out on success, CARA_ENOTFOUND if absent.
static int fw_cfg_find_ramfb(volatile u8 *regs, u16 *sel_out)
{
    // FW_CFG_FILE_DIR: be32 count, then `count` 64-byte entries of
    // { be32 size; be16 select; be16 reserved; char name[56]; }.
    u32 count_be = 0;
    int rc = fw_cfg_dma(regs,
                        ((u32)FW_CFG_FILE_DIR << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_READ,
                        4, &count_be);
    if (rc != CARA_EOK) {
        return rc;
    }
    u32 count = __builtin_bswap32(count_be);

    struct CARA_PACKED DirEntry {
        u32 size;   // be
        u16 select; // be
        u16 reserved;
        char name[56];
    };
    static struct DirEntry entry; // BSS scratch (reused each iteration)

    for (u32 i = 0; i < count; i++) {
        // Continued READ (no SELECT) advances through the directory.
        rc = fw_cfg_dma(regs, FW_CFG_DMA_CTL_READ, (u32)sizeof(entry), &entry);
        if (rc != CARA_EOK) {
            return rc;
        }
        if (name_is_etc_ramfb(entry.name)) {
            *sel_out = __builtin_bswap16(entry.select);
            return CARA_EOK;
        }
    }
    return CARA_ENOTFOUND;
}

[[nodiscard]] int Croi_Ramfb_Setup(struct DathFramebuffer *out, const struct Fdt *fdt,
                                   struct PageAllocator *pa, u32 width, u32 height)
{
    if (!out || !fdt || !pa || width == 0 || height == 0) {
        return CARA_EINVAL;
    }

    u32 node = 0;
    if (Fdt_FindByCompatible(fdt, "qemu,fw-cfg-mmio", &node) != CARA_EOK) {
        return CARA_ENOTFOUND;
    }
    u64 base = 0, size = 0;
    if (Fdt_PropReg(fdt, node, 0, &base, &size) != CARA_EOK) {
        return CARA_ENOTFOUND;
    }
    volatile u8 *regs = (volatile u8 *)Mm_PhysToVirt(base);

    u16 ramfb_sel = 0;
    int rc = fw_cfg_find_ramfb(regs, &ramfb_sel);
    if (rc != CARA_EOK) {
        return rc; // ENOTFOUND when QEMU was started without -device ramfb
    }

    u32 stride = width * 4;
    u64 fb_bytes = (u64)stride * height;
    u32 n_pages = (u32)((fb_bytes + CARA_PAGE_SIZE - 1) / CARA_PAGE_SIZE);
    u64 fb_phys = Page_Alloc(pa, n_pages);
    if (fb_phys == 0) {
        return CARA_ENOMEM;
    }

    struct RAMFBCfg cfg = {
        .addr = __builtin_bswap64(fb_phys),
        .fourcc = __builtin_bswap32(DRM_FORMAT_XRGB8888),
        .flags = 0,
        .width = __builtin_bswap32(width),
        .height = __builtin_bswap32(height),
        .stride = __builtin_bswap32(stride),
    };
    rc = fw_cfg_dma(regs, ((u32)ramfb_sel << 16) | FW_CFG_DMA_CTL_SELECT | FW_CFG_DMA_CTL_WRITE,
                    (u32)sizeof(cfg), &cfg);
    if (rc != CARA_EOK) {
        Page_Free(pa, fb_phys, n_pages);
        return rc;
    }

    rc = Dath_Framebuffer_Init(out, Mm_PhysToVirt(fb_phys), width, height, stride,
                               DATH_FMT_RGBA8888);
    if (rc != CARA_EOK) {
        Page_Free(pa, fb_phys, n_pages);
        return rc;
    }

    LOG_INFO("ramfb", "live %ux%u stride=%u fb_phys=0x%llx (%u pages) via fw-cfg@0x%llx", width,
             height, stride, fb_phys, n_pages, base);
    return CARA_EOK;
}
