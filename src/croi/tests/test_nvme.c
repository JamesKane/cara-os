// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(nvme_*): asserts the boot-time NVMe bring-up chain
// (Croi_Nvme_Probe → Setup → Identify → CreateIoQueues in entry.c)
// completed, then drives a write/readback through the I/O queue
// pair (docs/PHASE2_NVME.md N1–N5).
//
// Runs on QEMU virt with `-device nvme,drive=...,serial=...` backed
// by a throwaway raw image (smoke_qemu_kernel.sh creates one).
// Without the device these tests fail — expected, same caveat as
// the xHCI tests without usb-kbd/usb-mouse.

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/nvme.h>
#include <cara/test.h>
#include <cara/types.h>

extern struct NvmeController g_nvme;
extern bool g_nvme_probed;
extern struct PageAllocator g_page_alloc;

KERNEL_TEST(nvme_probe)
{
    TEST_ASSERT(ctx, g_nvme_probed, "Croi_Nvme_Probe did not run successfully at boot");
    TEST_ASSERT(ctx, (g_nvme.version >> 16) >= 1, "VS major < 1");
    TEST_ASSERT(ctx, g_nvme.bar0_phys != 0, "BAR0 not allocated");
    TEST_ASSERT(ctx, g_nvme.regs != nullptr, "register window not mapped");
    TEST_ASSERT(ctx, g_nvme.mqes >= 2, "CAP.MQES too small");
    TEST_ASSERT(ctx, g_nvme.css_nvm, "NVM command set not supported");

    // N2: admin queues programmed and CC.EN → CSTS.RDY observed.
    TEST_ASSERT(ctx, g_nvme.enabled, "controller did not reach CSTS.RDY=1");
    TEST_ASSERT(ctx, g_nvme.admin.sq_phys != 0, "ASQ not allocated");
    TEST_ASSERT(ctx, g_nvme.admin.cq_phys != 0, "ACQ not allocated");
}

KERNEL_TEST(nvme_identify)
{
    TEST_ASSERT(ctx, g_nvme_probed && g_nvme.enabled, "controller not up");

    // N3: Identify Controller strings + namespace geometry cached.
    TEST_ASSERT(ctx, g_nvme.ctrl_id.valid, "Identify Controller did not complete");
    TEST_ASSERT(ctx, g_nvme.ctrl_id.serial[0] != '\0', "controller serial empty");
    TEST_ASSERT(ctx, g_nvme.ctrl_id.n_namespaces >= 1, "controller reports zero namespaces");

    TEST_ASSERT(ctx, g_nvme.ns.valid, "Identify Namespace did not complete");
    TEST_ASSERT(ctx, g_nvme.ns.n_blocks > 0, "namespace has zero blocks");
    TEST_ASSERT(ctx, g_nvme.ns.block_bytes == 512 || g_nvme.ns.block_bytes == 4096,
                "unexpected LBA size (want 512 or 4096)");
}

KERNEL_TEST(nvme_io)
{
    TEST_ASSERT(ctx, g_nvme_probed && g_nvme.io_ready, "I/O queue pair not ready");
    TEST_ASSERT(ctx, g_nvme.ns.valid, "namespace geometry unknown");

    // Two pages out, two pages back — exercises the PRP2 path when
    // block_bytes × n_blocks spans both pages.
    u64 wbuf_phys = Page_Alloc(&g_page_alloc, 2);
    u64 rbuf_phys = Page_Alloc(&g_page_alloc, 2);
    TEST_ASSERT(ctx, wbuf_phys != 0 && rbuf_phys != 0, "DMA buffer allocation failed");
    u8 *wbuf = (u8 *)Mm_PhysToVirt(wbuf_phys);
    u8 *rbuf = (u8 *)Mm_PhysToVirt(rbuf_phys);

    u32 n_blocks = (u32)(2 * CARA_PAGE_SIZE / g_nvme.ns.block_bytes);
    u64 bytes = (u64)n_blocks * g_nvme.ns.block_bytes;

    // Land the pattern well clear of LBA 0 (a future CaraFS
    // superblock) but inside the smoke harness's small image: the
    // last n_blocks of the namespace.
    TEST_ASSERT(ctx, g_nvme.ns.n_blocks > n_blocks, "namespace too small for I/O test");
    u64 lba = g_nvme.ns.n_blocks - n_blocks;

    for (u64 i = 0; i < bytes; i++) {
        wbuf[i] = (u8)(0xA5u ^ (i * 7u) ^ (i >> 8));
    }

    int wrc = Croi_Nvme_Write(&g_nvme, lba, n_blocks, wbuf);
    TEST_ASSERT(ctx, wrc == CARA_EOK, "Croi_Nvme_Write failed");

    int rrc = Croi_Nvme_Read(&g_nvme, lba, n_blocks, rbuf);
    TEST_ASSERT(ctx, rrc == CARA_EOK, "Croi_Nvme_Read failed");

    bool match = true;
    for (u64 i = 0; i < bytes; i++) {
        if (rbuf[i] != wbuf[i]) {
            LOG_ERROR("nvts", "readback mismatch at byte %llu: 0x%x != 0x%x", (u64)i,
                      (unsigned)rbuf[i], (unsigned)wbuf[i]);
            match = false;
            break;
        }
    }
    TEST_ASSERT(ctx, match, "write/readback pattern mismatch");

    // Range validation: a read past the end of the namespace must be
    // rejected client-side, not handed to the controller.
    int erc = Croi_Nvme_Read(&g_nvme, g_nvme.ns.n_blocks, 1, rbuf);
    TEST_ASSERT(ctx, erc == CARA_ERANGE, "out-of-range read not rejected");

    Page_Free(&g_page_alloc, wbuf_phys, 2);
    Page_Free(&g_page_alloc, rbuf_phys, 2);

    LOG_INFO("nvts", "nvme at %x: %u blocks x %u B verified at lba %llu",
             ((u32)g_nvme.pci_bus << 16) | ((u32)g_nvme.pci_device << 8) | g_nvme.pci_function,
             (unsigned)n_blocks, (unsigned)g_nvme.ns.block_bytes, (u64)lba);
}
