// SPDX-License-Identifier: BSD-2-Clause
//
// Cara PCIe enumeration. Brand-namespace surface; the V36+ public face
// (an `expansion.library` analogue) lives separately and trampolines
// into the Croi_Pci_* primitives below.
//
// Discovery model: find a node compatible with `pci-host-ecam-generic`
// in the FDT, parse its ECAM base + bus-range + ranges, then walk every
// (bus, device, function) tuple in the bus-range looking for live
// functions. The PCIe ECAM region is already covered by the boot PT's
// L2[256] device leaf (1 GiB at PA 0), so config-space access is just
// arithmetic on Mm_PhysToVirt(ecam_base).
//
// Both QEMU virt's `pci-host-ecam-generic` host bridge and the X1's
// onboard PCIe controller (also ECAM-shaped per the X1 TRM) are served
// by this code path. There is no virtio shortcut.

#ifndef CARA_PCI_H
#define CARA_PCI_H

#include <cara/types.h>

struct Fdt;

// ---- Address ranges ---------------------------------------------------------
//
// Each `ranges` entry from the host bridge's FDT node maps a span of
// PCI bus addresses to a span of CPU physical addresses. The PCI side
// also carries an attribute word in the high cell (SS field 0..3 per
// the OF PCI binding):
//   0 = config space, 1 = I/O, 2 = 32-bit memory, 3 = 64-bit memory.
// CaraOS keeps I/O / MEM32 / MEM64 (config space is the ECAM, handled
// separately via reg).

typedef enum : u8 {
    PCI_RANGE_NONE  = 0,
    PCI_RANGE_IO    = 1,
    PCI_RANGE_MEM32 = 2,
    PCI_RANGE_MEM64 = 3,
} PciRangeKind;

struct PciRange {
    PciRangeKind kind;
    bool         prefetchable;
    u64          pci_addr;       // address as seen on the PCI bus
    u64          cpu_addr;       // CPU physical address
    u64          size;           // bytes
};

#define CARA_MAX_PCI_RANGES 8

// ---- Host bridge ------------------------------------------------------------

struct PciHostBridge {
    u64             ecam_base;       // CPU physical address of ECAM
    u64             ecam_size;       // bytes
    u8              bus_first;       // inclusive
    u8              bus_last;        // inclusive
    u32             n_ranges;
    struct PciRange range[CARA_MAX_PCI_RANGES];
};

// Discover the PCIe host bridge node in the FDT and populate `out`.
// Returns CARA_ENOTFOUND if no `pci-host-ecam-generic`-compatible node
// exists, CARA_EINVAL on malformed properties.
[[nodiscard]] int Croi_Pci_HostBridgeFromFdt(struct PciHostBridge *out,
                                             const struct Fdt *fdt);

// ---- Config-space accessors -------------------------------------------------
//
// Address arithmetic per PCIe 5.0 §7.2.2:
//   ecam_base + (bus << 20) + (device << 15) + (function << 12) + offset
//
// `bus`, `device`, `function`, `offset` must satisfy
//   bridge->bus_first <= bus <= bridge->bus_last
//   device < 32, function < 8, offset < 4096
// otherwise the read returns 0xFFFFFFFF (treated as "no device") and
// the write is silently dropped. Callers that care can pre-validate.

u8  Croi_Pci_Read8 (const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off);
u16 Croi_Pci_Read16(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off);
u32 Croi_Pci_Read32(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off);

void Croi_Pci_Write8 (const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off, u8  v);
void Croi_Pci_Write16(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off, u16 v);
void Croi_Pci_Write32(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off, u32 v);

// Common config-space offsets (PCI 3.0 §6.1).
#define PCI_CFG_VENDOR_ID    0x00     // u16
#define PCI_CFG_DEVICE_ID    0x02     // u16
#define PCI_CFG_COMMAND      0x04     // u16
#define PCI_CFG_STATUS       0x06     // u16
#define PCI_CFG_REVISION     0x08     // u8
#define PCI_CFG_PROG_IF      0x09     // u8
#define PCI_CFG_SUBCLASS     0x0A     // u8
#define PCI_CFG_BASE_CLASS   0x0B     // u8
#define PCI_CFG_CACHE_LINE   0x0C     // u8
#define PCI_CFG_LATENCY      0x0D     // u8
#define PCI_CFG_HEADER_TYPE  0x0E     // u8 (bit7 = multi-function)
#define PCI_CFG_BIST         0x0F     // u8
#define PCI_CFG_BAR0         0x10     // u32
#define PCI_CFG_BAR1         0x14
#define PCI_CFG_BAR2         0x18
#define PCI_CFG_BAR3         0x1C
#define PCI_CFG_BAR4         0x20
#define PCI_CFG_BAR5         0x24

#define PCI_HEADER_TYPE_GENERAL 0x00
#define PCI_HEADER_TYPE_BRIDGE  0x01
#define PCI_HEADER_TYPE_MULTI   0x80

#define PCI_CMD_IO_SPACE     0x0001
#define PCI_CMD_MEM_SPACE    0x0002
#define PCI_CMD_BUS_MASTER   0x0004

// Class triple for the xHCI USB host controller.
#define PCI_CLASS_USB_XHCI_BASE     0x0C
#define PCI_CLASS_USB_XHCI_SUB      0x03
#define PCI_CLASS_USB_XHCI_PROGIF   0x30

// ---- Discovered functions ---------------------------------------------------

#define CARA_MAX_PCI_BARS 6

struct PciBar {
    PciRangeKind kind;        // IO / MEM32 / MEM64
    bool         prefetchable;
    u64          base;        // CPU phys after BAR allocation; 0 if unused
    u64          size;
};

struct PciFunction {
    u8  bus;
    u8  device;
    u8  function;
    u8  header_type;          // raw; bit7 stripped in `multifunction` below
    bool multifunction;
    u16 vendor_id;
    u16 device_id;
    u8  base_class;
    u8  subclass;
    u8  prog_if;
    u8  revision;
    struct PciBar bar[CARA_MAX_PCI_BARS];
};

#define CARA_MAX_PCI_FUNCTIONS 32

struct PciInventory {
    struct PciHostBridge bridge;
    struct PciFunction   func[CARA_MAX_PCI_FUNCTIONS];
    u32                  n_funcs;
};

// One-shot bring-up. Discovers the host bridge from the FDT, walks the
// bus, sizes BARs, and populates `out`. Subsequent matches go through
// Croi_Pci_FindByClass below.
[[nodiscard]] int Croi_Pci_Init(struct PciInventory *out, const struct Fdt *fdt);

// Linear search for a function whose (base_class, subclass, prog_if)
// matches. `start_idx` is an in/out cursor — pass 0 first, the function
// writes the next index to resume from. Returns nullptr on no match
// (and leaves the cursor at n_funcs).
const struct PciFunction *
Croi_Pci_FindByClass(const struct PciInventory *inv, u8 base_class,
                     u8 subclass, u8 prog_if, u32 *cursor_inout);

#endif
