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

// ---- TRB layout (xHCI 1.2 §4.11 / §6.4) -----------------------------------
//
// Every TRB is 16 bytes = 4 × u32: parameter_lo, parameter_hi, status, control.
// The Cycle bit is control[0]; the TRB Type field is control[15:10].
// Software fills a TRB then advances the enqueue pointer; the controller
// reads them in order, advancing dequeue. Cycle bit toggles on wrap.

#define XHCI_TRB_BYTES                 16
#define XHCI_TRB_CYCLE                 (1u << 0)
#define XHCI_TRB_TYPE_SHIFT            10
#define XHCI_TRB_TYPE_MASK             0x3Fu
#define XHCI_TRB_TYPE(t)               (((u32)(t) & XHCI_TRB_TYPE_MASK) \
                                        << XHCI_TRB_TYPE_SHIFT)

// Transfer / command TRB types (xHCI 1.2 §6.4.6, Table 6-91).
#define XHCI_TRB_LINK                  6
#define XHCI_TRB_ENABLE_SLOT           9
#define XHCI_TRB_DISABLE_SLOT          10
#define XHCI_TRB_ADDRESS_DEVICE        11
#define XHCI_TRB_CONFIGURE_ENDPOINT    12
#define XHCI_TRB_NOOP_COMMAND          23

// Event TRB types (xHCI 1.2 §6.4.6).
#define XHCI_TRB_TRANSFER_EVENT        32
#define XHCI_TRB_COMMAND_COMPLETION    33
#define XHCI_TRB_PORT_STATUS_CHANGE    34

// Link TRB control flags (xHCI 1.2 §6.4.4.1).
#define XHCI_TRB_LINK_TC               (1u << 1)   // Toggle Cycle

// Address Device Command control flags (xHCI 1.2 §6.4.3.4).
//   BSR = Block Set Address Request: when 1 the controller does the
//   spec'd "set address only when CCS settles" dance. Phase 1 leaves
//   it 0 (issue SET_ADDRESS to the device immediately).
#define XHCI_TRB_ADDRDEV_BSR           (1u << 9)

// Slot ID is encoded in control[31:24] for slot-targeted commands.
#define XHCI_TRB_SLOT_ID_SHIFT         24

// Command Completion Event status fields (xHCI 1.2 §6.4.2.2).
//   status[31:24] = Completion Code
//   control[31:24] = Slot ID
#define XHCI_CC_SHIFT                  24
#define XHCI_CC_MASK                   0xFFu
#define XHCI_CC_SUCCESS                1
#define XHCI_CC_TRB_ERROR              5
#define XHCI_CC_CONTEXT_STATE_ERROR    19

// ---- Slot Context (xHCI 1.2 §6.2.2) ---------------------------------------
//
// 32 bytes when CSZ = 0, 64 bytes when CSZ = 1 (HCCPARAMS1.CSZ). Field
// offsets are the same; the upper half is reserved padding when CSZ = 1.
//
//   slot[0]  Route String[19:0] | Speed[23:20] | rsvd | MTT[25] | Hub[26]
//                                              | Context Entries[31:27]
//   slot[1]  Max Exit Latency[15:0] | Root Hub Port Number[23:16]
//                                                | Number of Ports[31:24]
//   slot[2]  Parent Hub Slot ID[7:0] | Parent Port Number[15:8]
//                                                | TTT[17:16] | rsvd
//                                                | Interrupter Target[31:22]
//   slot[3]  USB Device Address[7:0] | rsvd[26:8] | Slot State[31:27]

#define XHCI_SLOT_CTX_DW0_SPEED_SHIFT       20
#define XHCI_SLOT_CTX_DW0_CTXENTRIES_SHIFT  27
#define XHCI_SLOT_CTX_DW1_RHPORT_SHIFT      16
#define XHCI_SLOT_CTX_DW3_ADDRESS_MASK      0xFFu
#define XHCI_SLOT_CTX_DW3_STATE_SHIFT       27
#define XHCI_SLOT_CTX_DW3_STATE_MASK        0x1Fu

// Slot State values (xHCI 1.2 §4.5.3 / Table 4-15).
#define XHCI_SLOT_STATE_DISABLED            0
#define XHCI_SLOT_STATE_DEFAULT             1
#define XHCI_SLOT_STATE_ADDRESSED           2
#define XHCI_SLOT_STATE_CONFIGURED          3

// ---- Endpoint Context (xHCI 1.2 §6.2.3) -----------------------------------
//
// 32 bytes when CSZ = 0, 64 when CSZ = 1.
//
//   ep[0]  EP State[2:0] | rsvd | Mult[9:8] | MaxPStreams[14:10] | LSA[15]
//                                                  | Interval[23:16]
//                                                  | Max ESIT High[31:24]
//   ep[1]  rsvd[0] | CErr[2:1] | EP Type[5:3] | rsvd | HID[7] | Max Burst[15:8]
//                                                  | Max Packet Size[31:16]
//   ep[2]  TR Dequeue Pointer Lo (low bit = DCS, low 4 bits cleared)
//   ep[3]  TR Dequeue Pointer Hi
//   ep[4]  Average TRB Length[15:0] | Max ESIT Payload Lo[31:16]

#define XHCI_EP_CTX_DW1_CERR_SHIFT          1
#define XHCI_EP_CTX_DW1_EPTYPE_SHIFT        3
#define XHCI_EP_CTX_DW1_MAXPKT_SHIFT        16
#define XHCI_EP_CTX_DW2_DCS                 (1u << 0)

// EP Type values (xHCI 1.2 §6.2.3, Table 6-9). Phase 1 uses CONTROL
// (bidirectional) for EP0 and INTERRUPT_IN for HID interrupt-IN.
#define XHCI_EP_TYPE_ISOCH_OUT              1
#define XHCI_EP_TYPE_BULK_OUT               2
#define XHCI_EP_TYPE_INTERRUPT_OUT          3
#define XHCI_EP_TYPE_CONTROL                4
#define XHCI_EP_TYPE_ISOCH_IN               5
#define XHCI_EP_TYPE_BULK_IN                6
#define XHCI_EP_TYPE_INTERRUPT_IN           7

// Default EP0 Max Packet Size by speed (xHCI 1.2 §4.3.4 / §6.2.3.1).
// The controller will be told this value before the first GET_DESCRIPTOR;
// the descriptor's bMaxPacketSize0 may force a re-evaluate-context on
// FS devices, but Phase 1 picks the conservative correct value upfront.
#define XHCI_EP0_MAXPKT_LOW                 8     // LS: always 8
#define XHCI_EP0_MAXPKT_FULL                8     // FS: 8/16/32/64; 8 is safe
#define XHCI_EP0_MAXPKT_HIGH                64    // HS: always 64
#define XHCI_EP0_MAXPKT_SUPER               512   // SS+: always 512

// ---- Input Context (xHCI 1.2 §6.2.5) --------------------------------------
//
// Layout (CSZ = 0):
//   0x00..0x1F  Input Control Context (32 bytes)
//   0x20..0x3F  Slot Context           (= Device Slot Context offset)
//   0x40..0x5F  EP0 Context            (Default Control Pipe)
//   0x60..      EP1-OUT, EP1-IN, ... (DCI 2..31)
//
// Layout (CSZ = 1):
//   0x00..0x3F  Input Control Context (still 32 bytes; upper 32 padded)
//   0x40..0x7F  Slot Context
//   0x80..0xBF  EP0 Context
//   ...
//
// Input Control Context:
//   icc[0]  Drop Context Flags  (D0 reserved, D1 reserved, D2..D31 = endpoints)
//   icc[1]  Add  Context Flags  (A0 = Slot, A1 = EP0, A2..A31 = endpoints)
//   icc[2..6] reserved
//   icc[7]  Configuration Value | Interface Number | Alternate Setting

#define XHCI_INPUT_CTX_ADD_SLOT             (1u << 0)
#define XHCI_INPUT_CTX_ADD_EP0              (1u << 1)

// ---- Driver state -----------------------------------------------------------

#define CARA_XHCI_MAX_SLOTS                 32


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

    // Per-slot device state populated by Croi_Xhci_AddressConnectedDevices.
    // Slot IDs are 1-based; slot[0] is unused. Output Device Context is
    // installed in dcbaa[slot_id] so the controller writes Slot State /
    // assigned Address back as it processes commands.
    struct {
        bool          in_use;
        u8            root_port;       // 1-based xHCI port number
        u8            speed;            // XHCI_SPEED_*
        u8            usb_address;      // post-Address Device, from Slot Ctx
        u8            slot_state;       // XHCI_SLOT_STATE_*
        u64           output_ctx_phys;  // Output Device Context (DCBAA[slot])
        volatile u8  *output_ctx;
        u64           input_ctx_phys;   // Input Context (transient per command)
        volatile u8  *input_ctx;
        u64           ep0_ring_phys;    // EP0 (Default Control Pipe) Transfer Ring
        volatile u32 *ep0_ring;
        u32           ep0_ring_size_trbs;
        u32           ep0_ring_enqueue_idx;
        bool          ep0_ring_cycle;
    } slots[CARA_XHCI_MAX_SLOTS + 1];   // index 0 unused (slot IDs are 1-based)
    u32 n_addressed_slots;
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

// For each port that c->port[i].connected says is live, run the
// USB enumeration prologue defined by xHCI 1.2 §4.3:
//   1. Port Reset (PORTSC.PR), wait PRC, clear change bits.
//   2. Enable Slot command → slot ID.
//   3. Allocate Output Device Context, Input Context, EP0 Transfer Ring.
//   4. Address Device command (BSR=0): controller issues USB SET_ADDRESS
//      and writes the assigned address into the Output Slot Context.
//   5. Read back Slot State / USB Address into c->slots[slot_id].
// Populates c->n_addressed_slots. Idempotent — already-addressed slots
// are skipped on re-entry. Returns CARA_EOK if at least one device was
// successfully addressed (zero connected ports also returns EOK).
[[nodiscard]] int Croi_Xhci_AddressConnectedDevices(struct XhciController *c);

// Issue a Configure Endpoint command for slot `slot_id` using the
// Input Context whose physical address is `input_ctx_phys`. The
// caller is responsible for filling the Input Control Context (Add /
// Drop flags), the Slot Context copy, and the per-endpoint contexts
// before calling. Used in UC.5 once the HID interrupt-IN endpoint
// descriptor has been read.
[[nodiscard]] int Croi_Xhci_ConfigureEndpoint(struct XhciController *c,
                                              u8 slot_id,
                                              u64 input_ctx_phys);

#endif
