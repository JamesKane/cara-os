// SPDX-License-Identifier: BSD-2-Clause
//
// PCIe host-bridge discovery from the FDT.
//
// Parses a node compatible with `pci-host-ecam-generic` (the binding
// QEMU virt ships, and the same shape the X1's onboard PCIe
// controller exposes per its DT binding). Reads the ECAM base from
// `reg`, the valid bus span from `bus-range`, and decodes the
// `ranges` property into PciRange entries.
//
// QEMU virt's shape (captured from tests/data/qemu-virt.dtb), for
// reference:
//
//   pci@30000000 {
//       compatible      = "pci-host-ecam-generic";
//       reg             = <0x00 0x30000000 0x00 0x10000000>;     // 256 MiB ECAM
//       bus-range       = <0x00 0xff>;
//       ranges          = <
//           0x01000000 0x00 0x00000000  0x00 0x03000000  0x00 0x00010000  // I/O 64 KiB @ CPU 0x03000000
//           0x02000000 0x00 0x40000000  0x00 0x40000000  0x00 0x40000000  // MEM32 1 GiB @ CPU 0x40000000
//           0x03000000 0x04 0x00000000  0x04 0x00000000  0x04 0x00000000  // MEM64 16 GiB @ CPU 0x400000000
//       >;
//       #address-cells  = <0x03>;
//       #size-cells     = <0x02>;
//   };
//
// The ranges entry is 7 cells = 28 bytes:
//   phys.hi (3 cells encoded as 1 32-bit word) + phys.mid + phys.lo
//   cpu.hi  + cpu.lo
//   size.hi + size.lo
//
// phys.hi (the top 32 bits of the PCI address) carries OF-PCI binding
// flags in the high bits — bits 24..25 (`SS`) select the address
// space: 00=config, 01=I/O, 10=32-bit MEM, 11=64-bit MEM. Bit 30 is
// the prefetchable flag. The low 24 bits are the PCI bus / dev / fn /
// register the address starts at, only meaningful for type-1 ranges.
//
// We decode SS into PciRangeKind and prefetchable into a boolean;
// the rest of phys.hi is informational.

// No <cara/log.h> here — the parser is dual-target (host unit
// tests link cara_pci without cara_log). Boot-time logging
// happens in enum.c's Croi_Pci_Init.
#include <cara/fdt.h>
#include <cara/pci.h>
#include <cara/types.h>

static u32 be32_load(const void *p)
{
    const u8 *b = (const u8 *)p;
    return ((u32)b[0] << 24) | ((u32)b[1] << 16)
         | ((u32)b[2] <<  8) | ((u32)b[3]      );
}

[[nodiscard]] int Croi_Pci_HostBridgeFromFdt(struct PciHostBridge *out,
                                             const struct Fdt *fdt)
{
    if (!out || !fdt) {
        return CARA_EINVAL;
    }
    *out = (struct PciHostBridge){ 0 };

    u32 node = 0;
    int rc = Fdt_FindByCompatible(fdt, "pci-host-ecam-generic", &node);
    if (rc != CARA_EOK) {
        return CARA_ENOTFOUND;
    }

    // ECAM base + size from `reg` (parent #address-cells / #size-cells).
    u64 ecam_base = 0, ecam_size = 0;
    rc = Fdt_PropReg(fdt, node, 0, &ecam_base, &ecam_size);
    if (rc != CARA_EOK) {
        return rc;
    }
    out->ecam_base = ecam_base;
    out->ecam_size = ecam_size;

    // bus-range — two big-endian u32s.
    const void *bytes = nullptr;
    u32 len = 0;
    rc = Fdt_PropRaw(fdt, node, "bus-range", &bytes, &len);
    if (rc != CARA_EOK || len < 8) {
        return CARA_EINVAL;
    }
    u32 b_first = be32_load((const u8 *)bytes);
    u32 b_last  = be32_load((const u8 *)bytes + 4);
    if (b_first > 0xFF || b_last > 0xFF || b_first > b_last) {
        return CARA_EINVAL;
    }
    out->bus_first = (u8)b_first;
    out->bus_last  = (u8)b_last;

    // ranges — sequence of 7-cell entries.
    rc = Fdt_PropRaw(fdt, node, "ranges", &bytes, &len);
    if (rc != CARA_EOK) {
        return rc;
    }
    if ((len % 28) != 0) {
        return CARA_EINVAL;
    }
    u32 n_entries = len / 28;
    if (n_entries > CARA_MAX_PCI_RANGES) {
        n_entries = CARA_MAX_PCI_RANGES;
    }
    const u8 *p = (const u8 *)bytes;
    for (u32 i = 0; i < n_entries; i++) {
        u32 phys_hi = be32_load(p +  0);
        u32 phys_mi = be32_load(p +  4);
        u32 phys_lo = be32_load(p +  8);
        u32 cpu_hi  = be32_load(p + 12);
        u32 cpu_lo  = be32_load(p + 16);
        u32 size_hi = be32_load(p + 20);
        u32 size_lo = be32_load(p + 24);

        u32 ss = (phys_hi >> 24) & 0x03u;
        bool prefetchable = ((phys_hi >> 30) & 0x01u) != 0;
        PciRangeKind kind;
        switch (ss) {
        case 0: /* config space — not in our table */
            p += 28;
            continue;
        case 1: kind = PCI_RANGE_IO;    break;
        case 2: kind = PCI_RANGE_MEM32; break;
        case 3: kind = PCI_RANGE_MEM64; break;
        default:
            return CARA_EINVAL;
        }
        out->range[out->n_ranges++] = (struct PciRange){
            .kind         = kind,
            .prefetchable = prefetchable,
            .pci_addr     = ((u64)phys_mi << 32) | (u64)phys_lo,
            .cpu_addr     = ((u64)cpu_hi  << 32) | (u64)cpu_lo,
            .size         = ((u64)size_hi << 32) | (u64)size_lo,
        };
        p += 28;
    }

    if (out->n_ranges == 0) {
        // A host bridge with no MEM/IO ranges at all is malformed;
        // the spec mandates at least one.
        return CARA_EINVAL;
    }
    return CARA_EOK;
}
