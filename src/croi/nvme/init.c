// SPDX-License-Identifier: BSD-2-Clause
//
// NVMe controller probe + reset (N1). Walk the PCI function, size +
// allocate BAR0, parse CAP / VS, and run the disable sequence so the
// controller sits idle waiting for Croi_Nvme_Setup to configure the
// admin queues.
//
// NVM Express Base Specification 1.4 is the source for every
// register / field / sequence below.

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/nvme.h>
#include <cara/pci.h>
#include <cara/types.h>

#include "internal.h"

[[nodiscard]] int Croi_Nvme_Probe(struct NvmeController *out, struct PciInventory *inv,
                                  u32 func_index)
{
    if (!out || !inv || func_index >= inv->n_funcs) {
        return CARA_EINVAL;
    }
    *out = (struct NvmeController){ 0 };

    struct PciFunction *fn = &inv->func[func_index];
    if (fn->base_class != PCI_CLASS_NVME_BASE || fn->subclass != PCI_CLASS_NVME_SUB ||
        fn->prog_if != PCI_CLASS_NVME_PROGIF) {
        LOG_ERROR("nvme", "function %x is not class 0x01/0x08/0x02 (got %02x.%02x.%02x)",
                  ((u32)fn->bus << 16) | ((u32)fn->device << 8) | fn->function,
                  (unsigned)fn->base_class, (unsigned)fn->subclass, (unsigned)fn->prog_if);
        return CARA_EINVAL;
    }

    out->pci_bus = fn->bus;
    out->pci_device = fn->device;
    out->pci_function = fn->function;
    out->vendor_id = fn->vendor_id;
    out->device_id = fn->device_id;

    // BAR0 (the MLBAR/MUBAR pair — a 64-bit memory BAR) carries the
    // whole register map. The Phase 1 allocator places 64-bit BARs
    // inside the MEM32 window, which the boot PT already maps.
    int rc = Croi_Pci_AllocateBar(inv, func_index, 0);
    if (rc != CARA_EOK) {
        LOG_ERROR("nvme", "BAR0 allocation failed: %d", rc);
        return rc;
    }
    out->bar0_phys = fn->bar[0].base;
    out->bar0_size = fn->bar[0].size;
    out->regs = (volatile u8 *)Mm_PhysToVirt(out->bar0_phys);

    // Version gate: VS encodes major[31:16].minor[15:8] (Base §3.1.2).
    u32 vs = nvme_read32(out, NVME_REG_VS);
    out->version = vs;
    if ((vs >> 16) < 1) {
        LOG_ERROR("nvme", "VS 0x%x < 1.0; refusing", vs);
        return CARA_EBADVERSION;
    }

    u64 cap = nvme_read64(out, NVME_REG_CAP);
    out->mqes = (u32)(cap & NVME_CAP_MQES_MASK) + 1;
    out->timeout_500ms = (u32)((cap >> NVME_CAP_TO_SHIFT) & NVME_CAP_TO_MASK);
    out->dstrd = (u32)((cap >> NVME_CAP_DSTRD_SHIFT) & NVME_CAP_DSTRD_MASK);
    out->mpsmin = (u32)((cap >> NVME_CAP_MPSMIN_SHIFT) & NVME_CAP_MPSMIN_MASK);
    u32 css = (u32)((cap >> NVME_CAP_CSS_SHIFT) & NVME_CAP_CSS_MASK);
    out->css_nvm = (css & NVME_CAP_CSS_NVM) != 0;

    if (!out->css_nvm) {
        LOG_ERROR("nvme", "controller does not support the NVM command set (CSS=0x%x)", css);
        return CARA_EBADVERSION;
    }
    if (out->mpsmin != 0) {
        // MPSMIN > 0 means the controller can't do 4 KiB pages; no
        // real NVMe device ships like that, and our Page_Alloc story
        // assumes 4 KiB. Refuse loudly rather than mis-program MPS.
        LOG_ERROR("nvme", "CAP.MPSMIN=%u (min page > 4 KiB); refusing", (unsigned)out->mpsmin);
        return CARA_EBADVERSION;
    }

    LOG_INFO("nvme", "v%u.%u bar0=0x%llx (%llu KiB) mqes=%u dstrd=%u to=%ux500ms",
             (unsigned)(vs >> 16), (unsigned)((vs >> 8) & 0xFF), (u64)out->bar0_phys,
             (u64)(out->bar0_size >> 10), (unsigned)out->mqes, (unsigned)out->dstrd,
             (unsigned)out->timeout_500ms);

    // ---- Disable sequence (Base §3.1.5 / §7.6.1) ---------------------------
    //
    // If the controller is enabled (a warm reboot path), clear CC.EN
    // and wait for CSTS.RDY to deassert before touching the admin
    // queue registers — writing ASQ/ACQ while EN=1 is undefined.

    u32 cc = nvme_read32(out, NVME_REG_CC);
    if (cc & NVME_CC_EN) {
        nvme_write32(out, NVME_REG_CC, cc & ~NVME_CC_EN);
    }
    if (!nvme_spin_for_mask(out, NVME_REG_CSTS, NVME_CSTS_RDY, 0)) {
        LOG_ERROR("nvme", "CSTS.RDY did not deassert after CC.EN=0");
        return CARA_EAGAIN;
    }

    LOG_INFO("nvme", "reset complete; controller idle (CSTS=0x%x)",
             nvme_read32(out, NVME_REG_CSTS));
    return CARA_EOK;
}
