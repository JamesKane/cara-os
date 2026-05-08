// SPDX-License-Identifier: BSD-2-Clause
//
// xHCI host controller driver — kernel-internal surface.
//
// Implements the eXtensible Host Controller Interface (xHCI 1.2b
// specification, USB-IF) for any controller exposed via PCIe at
// class 0x0C / 0x03 / 0x30. Unifies QEMU's `qemu-xhci` (the daily
// driver) and the X1's onboard USB controller; the same code path
// runs on both because they're both spec-conformant xHCI 1.0+.
//
// Phase 1 first cut (this header + src/croi/xhci/init.c) brings the
// controller up to "reset complete" — capability registers parsed,
// USBCMD.HCRST sequence run, slot/port counts logged. The DCBAA /
// scratchpad / Command Ring / Event Ring / Run-Stop sequence and
// the port-status walk land in follow-on commits.

#ifndef CARA_XHCI_H
#define CARA_XHCI_H

#include <cara/types.h>

struct PciFunction;
struct PciInventory;

// ---- Capability registers (xHCI 1.2 §5.3, BAR0 + 0x00..0x1F) --------------
//
// First u32 of CAP space packs CAPLENGTH (low 8 bits = byte offset to
// op-regs from BAR0) and HCIVERSION (high 16 bits = BCD interface
// version, e.g. 0x0100 for xHCI 1.0).

#define XHCI_CAP_CAPLENGTH     0x00     // u8
#define XHCI_CAP_HCIVERSION    0x02     // u16
#define XHCI_CAP_HCSPARAMS1    0x04     // u32
#define XHCI_CAP_HCSPARAMS2    0x08     // u32
#define XHCI_CAP_HCSPARAMS3    0x0C     // u32
#define XHCI_CAP_HCCPARAMS1    0x10     // u32
#define XHCI_CAP_DBOFF         0x14     // u32 (lower 2 bits reserved)
#define XHCI_CAP_RTSOFF        0x18     // u32 (lower 5 bits reserved)
#define XHCI_CAP_HCCPARAMS2    0x1C     // u32

// HCSPARAMS1 field positions (xHCI 1.2 §5.3.3).
#define XHCI_HCSP1_MAX_SLOTS_SHIFT  0
#define XHCI_HCSP1_MAX_SLOTS_MASK   0xFFu
#define XHCI_HCSP1_MAX_INTRS_SHIFT  8
#define XHCI_HCSP1_MAX_INTRS_MASK   0x7FFu
#define XHCI_HCSP1_MAX_PORTS_SHIFT  24
#define XHCI_HCSP1_MAX_PORTS_MASK   0xFFu

// HCSPARAMS2.MaxScratchpadBuffers is split: high 5 bits at [25:21],
// low 5 bits at [31:27]. xHCI 1.2 §5.3.4.
#define XHCI_HCSP2_SCR_HI_SHIFT     21
#define XHCI_HCSP2_SCR_HI_MASK      0x1Fu
#define XHCI_HCSP2_SCR_LO_SHIFT     27
#define XHCI_HCSP2_SCR_LO_MASK      0x1Fu

#define XHCI_HCCP1_AC64             (1u << 0)
#define XHCI_HCCP1_CSZ              (1u << 2)   // 1 → 64-byte device contexts

// ---- Operational registers (xHCI 1.2 §5.4, BAR0 + CAPLENGTH) --------------

#define XHCI_OP_USBCMD         0x00
#define XHCI_OP_USBSTS         0x04
#define XHCI_OP_PAGESIZE       0x08
#define XHCI_OP_DNCTRL         0x14
#define XHCI_OP_CRCR_LO        0x18
#define XHCI_OP_CRCR_HI        0x1C
#define XHCI_OP_DCBAAP_LO      0x30
#define XHCI_OP_DCBAAP_HI      0x34
#define XHCI_OP_CONFIG         0x38

#define XHCI_OP_PORTSC_BASE    0x400
#define XHCI_OP_PORTSC_STRIDE  0x10

// USBCMD bits.
#define XHCI_USBCMD_RUN        (1u << 0)
#define XHCI_USBCMD_HCRST      (1u << 1)
#define XHCI_USBCMD_INTE       (1u << 2)
#define XHCI_USBCMD_HSEE       (1u << 3)

// USBSTS bits.
#define XHCI_USBSTS_HCH        (1u << 0)    // halted
#define XHCI_USBSTS_HSE        (1u << 2)    // host system error
#define XHCI_USBSTS_EINT       (1u << 3)    // event interrupt
#define XHCI_USBSTS_PCD        (1u << 4)    // port change detect
#define XHCI_USBSTS_CNR        (1u << 11)   // controller not ready
#define XHCI_USBSTS_HCE        (1u << 12)   // host controller error

// CONFIG.MaxSlotsEn occupies the low 8 bits.
#define XHCI_CONFIG_MAXSLOTSEN_MASK 0xFFu

// ---- Driver state -----------------------------------------------------------

struct XhciController {
    // PCI identity.
    u8  pci_bus;
    u8  pci_device;
    u8  pci_function;
    u16 vendor_id;
    u16 device_id;

    // BAR0 mapping.
    u64           bar0_phys;
    u64           bar0_size;
    volatile u8  *cap_regs;        // kernel VA = Mm_PhysToVirt(bar0_phys)
    volatile u8  *op_regs;         // = cap_regs + CAPLENGTH
    volatile u8  *runtime_regs;    // = cap_regs + (RTSOFF & ~0x1F)
    volatile u32 *doorbells;       // = cap_regs + (DBOFF & ~0x3)

    // Capability snapshot.
    u16 hci_version;
    u32 max_slots;
    u32 max_intrs;
    u32 max_ports;
    u32 page_size_bytes;           // PAGESIZE * 4096 effectively
    u32 max_scratchpad_buffers;
    bool ac64;
    bool csz_64;                   // 64-byte device contexts (vs 32-byte)
};

// Discover and reset the xHCI controller behind `func_index` in
// `inv`. Allocates BAR0, computes register-window offsets, reads
// the capability registers, and runs the USBCMD.HCRST sequence
// until USBSTS.CNR clears. Logs the slot/port counts via
// LOG_INFO. Returns:
//   CARA_EOK     — controller is reset and idle (USBSTS.HCH set)
//   CARA_ENOENT  — BAR0 is not implemented on the function
//   CARA_ENOMEM  — MEM32 range exhausted during BAR allocation
//   CARA_EINVAL  — function isn't class 0x0C/0x03/0x30, weird BAR
//   CARA_EBADVERSION — HCIVERSION < 0x100 (pre-1.0 controller)
[[nodiscard]] int Croi_Xhci_Probe(struct XhciController *out,
                                  struct PciInventory   *inv,
                                  u32                    func_index);

#endif
