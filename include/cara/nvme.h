// SPDX-License-Identifier: BSD-2-Clause
//
// NVMe controller driver — kernel-internal surface.
//
// Implements the NVM Express Base Specification (rev 1.4) admin +
// NVM command sets for any controller exposed via PCIe at class
// 0x01 / 0x08 / 0x02. Unifies QEMU's `-device nvme` (the daily
// driver) and whatever M.2 SSD sits in the RV2's slot; both are
// spec-conformant NVMe so the same code path runs on each.
//
// Section references in comments are to the NVM Express Base
// Specification 1.4 ("Base §") and the NVM Command Set parts of the
// same document. Phase 2 first cut is polled completion only — no
// MSI-X — mirroring the Phase 1 xHCI driver's approach.

#ifndef CARA_NVME_H
#define CARA_NVME_H

#include <cara/types.h>

struct PciInventory;

// Class triple for an NVMe controller (NVM Express over PCIe,
// I/O command set; PCI Code and ID Assignment §1.1).
enum : u8 {
    PCI_CLASS_NVME_BASE = 0x01,
    PCI_CLASS_NVME_SUB = 0x08,
    PCI_CLASS_NVME_PROGIF = 0x02,
};

// ---- Controller registers (Base §3.1, BAR0) --------------------------------

enum : u32 {
    NVME_REG_CAP = 0x00,   // u64  Controller Capabilities
    NVME_REG_VS = 0x08,    // u32  Version
    NVME_REG_INTMS = 0x0C, // u32  Interrupt Mask Set
    NVME_REG_INTMC = 0x10, // u32  Interrupt Mask Clear
    NVME_REG_CC = 0x14,    // u32  Controller Configuration
    NVME_REG_CSTS = 0x1C,  // u32  Controller Status
    NVME_REG_AQA = 0x24,   // u32  Admin Queue Attributes
    NVME_REG_ASQ = 0x28,   // u64  Admin Submission Queue Base
    NVME_REG_ACQ = 0x30,   // u64  Admin Completion Queue Base
};

// Doorbell array starts at BAR0 + 0x1000 (Base §3.1.16):
//   SQ y Tail doorbell at 0x1000 + (2y)     * (4 << CAP.DSTRD)
//   CQ y Head doorbell at 0x1000 + (2y + 1) * (4 << CAP.DSTRD)
constexpr u32 NVME_DOORBELL_BASE = 0x1000;

// CAP fields (Base §3.1.1).
enum : u64 {
    NVME_CAP_MQES_MASK = 0xFFFFull, // [15:0]  max queue entries - 1
    NVME_CAP_CQR = (1ull << 16),    // contiguous queues required
    NVME_CAP_TO_SHIFT = 24,         // [31:24] timeout, 500 ms units
    NVME_CAP_TO_MASK = 0xFFull,
    NVME_CAP_DSTRD_SHIFT = 32, // [35:32] doorbell stride = 4 << n
    NVME_CAP_DSTRD_MASK = 0xFull,
    NVME_CAP_CSS_SHIFT = 37, // [44:37] supported command sets
    NVME_CAP_CSS_MASK = 0xFFull,
    NVME_CAP_CSS_NVM = 0x01,    // bit 0 = NVM command set
    NVME_CAP_MPSMIN_SHIFT = 48, // [51:48] min page = 2^(12+n)
    NVME_CAP_MPSMIN_MASK = 0xFull,
    NVME_CAP_MPSMAX_SHIFT = 52, // [55:52]
    NVME_CAP_MPSMAX_MASK = 0xFull,
};

// CC fields (Base §3.1.5).
enum : u32 {
    NVME_CC_EN = (1u << 0),
    NVME_CC_CSS_NVM = (0u << 4),   // [6:4] = 000b → NVM command set
    NVME_CC_MPS_SHIFT = 7,         // [10:7] page = 2^(12+n); 0 = 4 KiB
    NVME_CC_AMS_RR = (0u << 11),   // [13:11] round-robin arbitration
    NVME_CC_SHN_NONE = (0u << 14), // [15:14] no shutdown notification
    NVME_CC_IOSQES_SHIFT = 16,     // [19:16] SQ entry = 2^n; 6 → 64 B
    NVME_CC_IOCQES_SHIFT = 20,     // [23:20] CQ entry = 2^n; 4 → 16 B
};
constexpr u32 NVME_SQE_SHIFT = 6; // 64-byte submission entries
constexpr u32 NVME_CQE_SHIFT = 4; // 16-byte completion entries

// CSTS fields (Base §3.1.6).
enum : u32 {
    NVME_CSTS_RDY = (1u << 0),
    NVME_CSTS_CFS = (1u << 1), // controller fatal status
    NVME_CSTS_SHST_SHIFT = 2,  // [3:2] shutdown status
    NVME_CSTS_SHST_MASK = 0x3u,
};

// AQA fields (Base §3.1.8): sizes are 0-based entry counts.
enum : u32 {
    NVME_AQA_ASQS_SHIFT = 0,  // [11:0]
    NVME_AQA_ACQS_SHIFT = 16, // [27:16]
};

// ---- Commands (Base §4.2, §4.6) ---------------------------------------------
//
// Submission Queue Entry: 64 bytes = 16 dwords.
//   CDW0: opcode[7:0] | FUSE[9:8] | PSDT[15:14] | CID[31:16]
//   CDW1: NSID
//   CDW6/7:  PRP1 (or 8 bytes at byte offset 24)
//   CDW8/9:  PRP2
//   CDW10..15: command-specific.
// Completion Queue Entry: 16 bytes = 4 dwords.
//   DW0: command-specific
//   DW2: SQ Head Pointer[15:0] | SQ ID[31:16]
//   DW3: CID[15:0] | Phase[16] | Status[31:17]

constexpr u32 NVME_SQE_DWORDS = 16;
constexpr u32 NVME_CQE_DWORDS = 4;

constexpr u32 NVME_CQE_DW3_CID_MASK = 0xFFFFu;
constexpr u32 NVME_CQE_DW3_PHASE = (1u << 16);
constexpr u32 NVME_CQE_DW3_STATUS_SHIFT = 17; // SC[8:1 of status field]

// Admin opcodes (Base §5).
enum : u8 {
    NVME_ADMIN_DELETE_IO_SQ = 0x00,
    NVME_ADMIN_CREATE_IO_SQ = 0x01,
    NVME_ADMIN_GET_LOG_PAGE = 0x02,
    NVME_ADMIN_DELETE_IO_CQ = 0x04,
    NVME_ADMIN_CREATE_IO_CQ = 0x05,
    NVME_ADMIN_IDENTIFY = 0x06,
    NVME_ADMIN_SET_FEATURES = 0x09,
};

// NVM command set opcodes (NVM Command Set §3).
enum : u8 {
    NVME_NVM_FLUSH = 0x00,
    NVME_NVM_WRITE = 0x01,
    NVME_NVM_READ = 0x02,
};

// Identify CNS values (Base §5.15.1).
enum : u8 {
    NVME_CNS_NAMESPACE = 0x00,
    NVME_CNS_CONTROLLER = 0x01,
    NVME_CNS_ACTIVE_NS_LIST = 0x02,
};

// Identify Controller data structure offsets (Base §5.15.2, Fig 251).
enum : u32 {
    NVME_IDCTRL_SN_OFF = 4,   // 20 ASCII bytes
    NVME_IDCTRL_MN_OFF = 24,  // 40 ASCII bytes
    NVME_IDCTRL_FR_OFF = 64,  // 8 ASCII bytes
    NVME_IDCTRL_NN_OFF = 516, // u32 number of namespaces
};

// Identify Namespace data structure offsets (NVM Command Set, Fig 245).
enum : u32 {
    NVME_IDNS_NSZE_OFF = 0,   // u64 namespace size in logical blocks
    NVME_IDNS_NCAP_OFF = 8,   // u64
    NVME_IDNS_NLBAF_OFF = 25, // u8  number of LBA formats - 1
    NVME_IDNS_FLBAS_OFF = 26, // u8  formatted LBA size; [3:0] = format idx
    NVME_IDNS_LBAF_OFF = 128, // 4-byte entries; [23:16] = LBADS (log2 size)
};

// Create I/O CQ / SQ CDW10/11 fields (Base §5.4 / §5.5).
enum : u32 {
    NVME_CREATEQ_QSIZE_SHIFT = 16, // CDW10[31:16] 0-based size
    NVME_CREATEQ_PC = (1u << 0),   // CDW11: physically contiguous
    NVME_CREATECQ_IEN = (1u << 1), // CDW11: interrupts enabled (we keep 0)
    NVME_CREATESQ_CQID_SHIFT = 16, // CDW11[31:16] completion queue id
};

// ---- Driver state -----------------------------------------------------------

// Queue sizes. One 4 KiB page holds 64 SQEs or 256 CQEs; sizing both
// at 64 entries keeps each queue a single page and is far more than
// the polled Phase 2 driver ever has in flight.
constexpr u32 CARA_NVME_QUEUE_ENTRIES = 64;

// Fixed identity of the one namespace Phase 2 drives. QEMU's nvme
// device and every single-namespace M.2 SSD expose NSID 1.
constexpr u32 CARA_NVME_NSID = 1;

struct NvmeQueue {
    u64 sq_phys;
    volatile u32 *sq; // CARA_NVME_QUEUE_ENTRIES × 16 dwords
    u64 cq_phys;
    volatile u32 *cq; // CARA_NVME_QUEUE_ENTRIES × 4 dwords
    u32 entries;
    u16 qid;
    u16 sq_tail;
    u16 cq_head;
    bool cq_phase; // expected Phase Tag of the next new CQE
    u16 next_cid;
};

struct NvmeController {
    // PCI identity.
    u8 pci_bus;
    u8 pci_device;
    u8 pci_function;
    u16 vendor_id;
    u16 device_id;

    // BAR0 mapping.
    u64 bar0_phys;
    u64 bar0_size;
    volatile u8 *regs; // kernel VA = Mm_PhysToVirt(bar0_phys)

    // Capability snapshot.
    u32 version;       // VS register: major[31:16].minor[15:8]
    u32 mqes;          // max queue entries (1-based)
    u32 dstrd;         // doorbell stride exponent
    u32 timeout_500ms; // CAP.TO
    u32 mpsmin;        // CAP.MPSMIN (page = 2^(12+n))
    bool css_nvm;      // NVM command set supported

    // Queues (N2 admin, N4 I/O qid 1).
    struct NvmeQueue admin;
    struct NvmeQueue io;
    bool enabled;  // CSTS.RDY observed after CC.EN=1
    bool io_ready; // Create I/O CQ + SQ both completed

    // One-page DMA scratch buffer for Identify reads.
    u64 dma_buf_phys;
    volatile u8 *dma_buf;

    // Identify Controller snapshot (N3). ASCII fields are
    // space-trimmed and NUL-terminated.
    struct {
        bool valid;
        char serial[21];
        char model[41];
        char firmware[9];
        u32 n_namespaces;
    } ctrl_id;

    // Identify Namespace snapshot for NSID 1 (N3).
    struct {
        bool valid;
        u64 n_blocks;    // NSZE
        u32 block_bytes; // 2^LBADS of the active LBA format
    } ns;
};

// N1: discover + reset. Allocates BAR0, maps the register window,
// parses CAP/VS, and runs the disable sequence (CC.EN=0 → CSTS.RDY=0)
// so the controller is in a known idle state. Returns:
//   CARA_EOK         — controller reset and idle
//   CARA_ENOENT      — BAR0 not implemented
//   CARA_ENOMEM      — MEM32 range exhausted during BAR allocation
//   CARA_EINVAL      — wrong class triple / malformed BAR
//   CARA_EBADVERSION — VS < 1.0 or NVM command set unsupported
//   CARA_EAGAIN      — CSTS.RDY never deasserted (CAP.TO expired)
[[nodiscard]] int Croi_Nvme_Probe(struct NvmeController *out, struct PciInventory *inv,
                                  u32 func_index);

// N2: allocate the admin queue pair + DMA scratch page, program
// AQA/ASQ/ACQ, configure CC (NVM command set, 4 KiB MPS, 64-byte
// SQEs / 16-byte CQEs), set CC.EN and wait for CSTS.RDY. On success
// c->enabled = true and admin commands may be submitted.
[[nodiscard]] int Croi_Nvme_Setup(struct NvmeController *c);

// Submit one admin command and poll for its completion (Base §7.2.1).
// `cdw0_opc` is the opcode only — CID is assigned internally. Returns
// CARA_EOK on Status Field == 0, CARA_EIO on a non-zero status (logged),
// CARA_EAGAIN on poll timeout. `cqe_dw0_out` (if non-NULL) receives
// completion DW0.
[[nodiscard]] int Croi_Nvme_AdminCmd(struct NvmeController *c, u8 opcode, u32 nsid, u64 prp1,
                                     u64 prp2, u32 cdw10, u32 cdw11, u32 *cqe_dw0_out);

// N3: Identify Controller (CNS 1) + Identify Namespace (CNS 0,
// NSID 1). Populates c->ctrl_id and c->ns and logs the SN/MN/FR
// strings and namespace geometry.
[[nodiscard]] int Croi_Nvme_Identify(struct NvmeController *c);

// N4: create the polled I/O queue pair (QID 1) via Create I/O CQ /
// Create I/O SQ admin commands. On success c->io_ready = true.
[[nodiscard]] int Croi_Nvme_CreateIoQueues(struct NvmeController *c);

// N5: synchronous block I/O on NSID 1 through the I/O queue pair.
// `buf` must be a kernel direct-map pointer, page-aligned, covering
// n_blocks × block_bytes ≤ 8 KiB (PRP1 + PRP2, no PRP list yet).
// Returns CARA_EOK, CARA_EIO (non-zero NVMe status), CARA_EAGAIN
// (timeout), or CARA_EINVAL (range/alignment/state).
[[nodiscard]] int Croi_Nvme_Read(struct NvmeController *c, u64 lba, u32 n_blocks, void *buf);
[[nodiscard]] int Croi_Nvme_Write(struct NvmeController *c, u64 lba, u32 n_blocks, const void *buf);

// F5: NVM Flush (opcode 0x00) on NSID 1 through the I/O queue pair —
// commit volatile write cache to the medium. The durability barrier
// CaraFS's journal relies on (CARAFS.md §3.9). Returns CARA_EOK,
// CARA_EIO (non-zero NVMe status), CARA_EAGAIN (timeout), or
// CARA_EINVAL (I/O queue not ready).
[[nodiscard]] int Croi_Nvme_Flush(struct NvmeController *c);

#endif
