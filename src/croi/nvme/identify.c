// SPDX-License-Identifier: BSD-2-Clause
//
// NVMe Identify (N3): Identify Controller (CNS 1) for the SN/MN/FR
// strings and namespace count, Identify Namespace (CNS 0, NSID 1)
// for the geometry CaraFS will sit on. Base §5.15; Identify
// Namespace data structure per the NVM Command Set figure set.

#include <cara/log.h>
#include <cara/nvme.h>
#include <cara/types.h>

#include "internal.h"

static u32 le32_at(const volatile u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u64 le64_at(const volatile u8 *p)
{
    return (u64)le32_at(p) | ((u64)le32_at(p + 4) << 32);
}

// Identify's ASCII fields are fixed-width, space-padded, not
// NUL-terminated (Base §1.5 "ASCII string"). Copy + trim trailing
// spaces + terminate.
static void ascii_field(char *dst, const volatile u8 *src, u32 len)
{
    u32 end = len;
    while (end > 0 && (src[end - 1] == ' ' || src[end - 1] == 0)) {
        end--;
    }
    for (u32 i = 0; i < end; i++) {
        dst[i] = (char)src[i];
    }
    dst[end] = '\0';
}

[[nodiscard]] int Croi_Nvme_Identify(struct NvmeController *c)
{
    if (!c || !c->enabled || !c->dma_buf) {
        return CARA_EINVAL;
    }

    // ---- Identify Controller (CNS 0x01, Base §5.15.2.2) -------------------
    // The data structure is exactly one 4 KiB page → PRP1 only.
    int rc = Croi_Nvme_AdminCmd(c, NVME_ADMIN_IDENTIFY, 0, c->dma_buf_phys, 0, NVME_CNS_CONTROLLER,
                                0, nullptr);
    if (rc != CARA_EOK) {
        LOG_ERROR("nvme", "Identify Controller failed: %d", rc);
        return rc;
    }
    ascii_field(c->ctrl_id.serial, c->dma_buf + NVME_IDCTRL_SN_OFF, 20);
    ascii_field(c->ctrl_id.model, c->dma_buf + NVME_IDCTRL_MN_OFF, 40);
    ascii_field(c->ctrl_id.firmware, c->dma_buf + NVME_IDCTRL_FR_OFF, 8);
    c->ctrl_id.n_namespaces = le32_at(c->dma_buf + NVME_IDCTRL_NN_OFF);
    c->ctrl_id.valid = true;

    LOG_INFO("nvme", "controller: model='%s' serial='%s' fw='%s' namespaces=%u", c->ctrl_id.model,
             c->ctrl_id.serial, c->ctrl_id.firmware, (unsigned)c->ctrl_id.n_namespaces);

    if (c->ctrl_id.n_namespaces < CARA_NVME_NSID) {
        LOG_ERROR("nvme", "controller reports no namespace %u", (unsigned)CARA_NVME_NSID);
        return CARA_ENOENT;
    }

    // ---- Identify Namespace (CNS 0x00, NSID 1) -----------------------------
    rc = Croi_Nvme_AdminCmd(c, NVME_ADMIN_IDENTIFY, CARA_NVME_NSID, c->dma_buf_phys, 0,
                            NVME_CNS_NAMESPACE, 0, nullptr);
    if (rc != CARA_EOK) {
        LOG_ERROR("nvme", "Identify Namespace %u failed: %d", (unsigned)CARA_NVME_NSID, rc);
        return rc;
    }

    u64 nsze = le64_at(c->dma_buf + NVME_IDNS_NSZE_OFF);
    if (nsze == 0) {
        LOG_ERROR("nvme", "namespace %u has zero size (inactive?)", (unsigned)CARA_NVME_NSID);
        return CARA_ENOENT;
    }

    // FLBAS[3:0] indexes the LBA-format table; each 4-byte LBAF entry
    // carries LBADS (log2 of the data size) in bits [23:16]. LBADS < 9
    // is spec-invalid (smallest LBA is 512 B).
    u8 flbas = c->dma_buf[NVME_IDNS_FLBAS_OFF];
    u32 fmt_idx = flbas & 0xFu;
    u32 lbaf = le32_at(c->dma_buf + NVME_IDNS_LBAF_OFF + 4u * fmt_idx);
    u32 lbads = (lbaf >> 16) & 0xFFu;
    if (lbads < 9 || lbads > 16) {
        LOG_ERROR("nvme", "LBA format %u has bad LBADS=%u", (unsigned)fmt_idx, (unsigned)lbads);
        return CARA_EINVAL;
    }

    c->ns.n_blocks = nsze;
    c->ns.block_bytes = 1u << lbads;
    c->ns.valid = true;

    LOG_INFO("nvme", "nsid %u: %llu blocks x %u B (%llu MiB)", (unsigned)CARA_NVME_NSID,
             (u64)c->ns.n_blocks, (unsigned)c->ns.block_bytes,
             (u64)((c->ns.n_blocks * c->ns.block_bytes) >> 20));
    return CARA_EOK;
}
