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

// Runtime register offsets (at RTSOFF, masked low 5 bits).
#define XHCI_RT_MFINDEX        0x0000
#define XHCI_RT_INTR0_BASE     0x0020   // Interrupter 0 register set
#define XHCI_RT_INTR_STRIDE    0x0020   // 32 bytes per interrupter
#define XHCI_RT_INTR_IMAN      0x00
#define XHCI_RT_INTR_IMOD      0x04
#define XHCI_RT_INTR_ERSTSZ    0x08
#define XHCI_RT_INTR_ERSTBA_LO 0x10
#define XHCI_RT_INTR_ERSTBA_HI 0x14
#define XHCI_RT_INTR_ERDP_LO   0x18
#define XHCI_RT_INTR_ERDP_HI   0x1C

// CRCR control bits (xHCI 1.2 §5.4.5).
#define XHCI_CRCR_RCS          (1u << 0)   // Ring Cycle State (initial)
#define XHCI_CRCR_CS           (1u << 1)   // Command Stop
#define XHCI_CRCR_CA           (1u << 2)   // Command Abort
#define XHCI_CRCR_CRR          (1u << 3)   // Command Ring Running (RO)

// PORTSC bits (xHCI 1.2 §5.4.8.1).
#define XHCI_PORTSC_CCS        (1u << 0)   // Current Connect Status
#define XHCI_PORTSC_PED        (1u << 1)   // Port Enabled (RW1C)
#define XHCI_PORTSC_OCA        (1u << 3)   // Overcurrent Active
#define XHCI_PORTSC_PR         (1u << 4)   // Port Reset (RW1S)
#define XHCI_PORTSC_PLS_SHIFT  5
#define XHCI_PORTSC_PLS_MASK   0xFu
#define XHCI_PORTSC_PP         (1u << 9)   // Port Power
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK  0xFu
// Change bits — RW1C, software clears by writing 1. To preserve
// state when modifying other PORTSC fields, mask with RW1C_MASK.
#define XHCI_PORTSC_CSC        (1u << 17)  // Connect Status Change
#define XHCI_PORTSC_PEC        (1u << 18)  // Port Enable Change
#define XHCI_PORTSC_WRC        (1u << 19)  // Warm Port Reset Change
#define XHCI_PORTSC_OCC        (1u << 20)  // Overcurrent Change
#define XHCI_PORTSC_PRC        (1u << 21)  // Port Reset Change
#define XHCI_PORTSC_PLC        (1u << 22)  // Port Link State Change
#define XHCI_PORTSC_CEC        (1u << 23)  // Port Configuration Error Change
#define XHCI_PORTSC_RW1C_MASK \
    (XHCI_PORTSC_PED | XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | \
     XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | \
     XHCI_PORTSC_PLC | XHCI_PORTSC_CEC)

// Port Speed values (xHCI 1.2 §5.4.8.1.5). Match the default USB
// speed-id assignments most controllers (incl. qemu-xhci) ship.
#define XHCI_SPEED_FULL        1   // USB 2.0 Full-speed (12 Mbps)
#define XHCI_SPEED_LOW         2   // USB 2.0 Low-speed (1.5 Mbps)
#define XHCI_SPEED_HIGH        3   // USB 2.0 High-speed (480 Mbps)
#define XHCI_SPEED_SUPER       4   // USB 3.0 SuperSpeed (5 Gbps)
#define XHCI_SPEED_SUPER_PLUS  5   // USB 3.1 SuperSpeedPlus (10 Gbps)

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

    // ---- Setup state (allocated by Croi_Xhci_Setup) -----------------------
    //
    // All buffers are allocated via Page_Alloc, naturally page-aligned
    // (which satisfies xHCI's 64-byte alignment requirement). The
    // controller DMAs to/from these regions, so the kernel-VA pointers
    // and the CPU-physical addresses are both kept on hand: Mm_PhysToVirt
    // gives the *_kva from *_phys for cleanup.

    u64           dcbaa_phys;          // Device Context Base Address Array
    volatile u64 *dcbaa;               // (max_slots + 1) entries

    u64           scratchpad_array_phys;
    volatile u64 *scratchpad_array;
    u64           scratchpad_buf_phys[64]; // up to 64 scratchpad buffers
    u32           n_scratchpad_bufs;

    u64           cmd_ring_phys;
    volatile u32 *cmd_ring;
    u32           cmd_ring_size_trbs;
    u32           cmd_ring_enqueue_idx;
    bool          cmd_ring_cycle;

    u64           erst_phys;           // Event Ring Segment Table (Interrupter 0)
    volatile u64 *erst;
    u64           event_ring_phys;     // Event Ring Segment 0
    volatile u32 *event_ring;
    u32           event_ring_size_trbs;
    u32           event_ring_dequeue_idx;
    bool          event_ring_cycle;

    bool running;                       // USBSTS.HCH cleared after Setup

    // Per-port status populated by Croi_Xhci_PortsWalk. Indexed
    // 0..max_ports-1; xHCI 1.2 numbers ports 1..N in user docs but
    // 0-based in the PORTSC register array.
    struct {
        bool connected;
        bool enabled;
        u8   speed;          // XHCI_SPEED_* or 0 if undefined
        u8   link_state;     // raw PLS field
    } port[256];
    u32 n_connected_ports;
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

// Bring the (already-Probe'd) controller to "running" state per
// xHCI 1.2 §4.2:
//   1. CONFIG.MaxSlotsEn = max_slots
//   2. Allocate DCBAA + scratchpad bufs; program DCBAAP
//   3. Allocate Command Ring; program CRCR
//   4. Allocate Event Ring Segment + ERST for Interrupter 0;
//      program ERSTSZ, ERDP, ERSTBA
//   5. Set USBCMD.RUN = 1
//   6. Wait USBSTS.HCH = 0
// Returns CARA_EOK on success (c->running = true).
[[nodiscard]] int Croi_Xhci_Setup(struct XhciController *c);

// Walk every PORTSC register, decode CCS / PED / PLS / Speed, and
// populate c->port[]. Logs each connected port. Updates
// c->n_connected_ports. Must be called after Croi_Xhci_Setup so
// the controller is running and CCS reflects the live link state.
[[nodiscard]] int Croi_Xhci_PortsWalk(struct XhciController *c);

#endif
