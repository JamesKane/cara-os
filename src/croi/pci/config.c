// SPDX-License-Identifier: BSD-2-Clause
//
// PCIe ECAM config-space accessors.
//
// ECAM ("Enhanced Configuration Access Mechanism", PCIe 5.0 §7.2.2)
// maps the 4 KiB config space of every (bus, device, function) tuple
// into a contiguous CPU physical region:
//
//   addr = ecam_base + (bus << 20) + (device << 15) + (function << 12) + offset
//
// The kernel's boot PT's L2[256] entry is a 1 GiB device-attribute leaf
// covering PA 0..0x4000_0000, which contains both QEMU virt's ECAM at
// PA 0x3000_0000 and any reasonable platform ECAM placement. Mm_PhysToVirt
// gives us the upper-half kernel VA without any extra mapping work.
//
// Reads from a non-present (bus, device, function) return 0xFF...FF per
// the PCI spec; we don't bother short-circuiting that here — the bus
// walker uses VID == 0xFFFF as the "no device" sentinel.
//
// Out-of-range arguments (bus outside the bus-range, dev > 31, fn > 7,
// off >= 4096) are treated as "no device" reads (return ~0) and dropped
// writes. Callers that care can pre-validate.

#include <cara/log.h>
#include <cara/mm.h>
#include <cara/pci.h>
#include <cara/types.h>

static volatile u8 *ecam_addr(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off)
{
    if (!b || b->ecam_base == 0) {
        return nullptr;
    }
    if (bus < b->bus_first || bus > b->bus_last) {
        return nullptr;
    }
    if (dev > 31 || fn > 7 || off >= 4096) {
        return nullptr;
    }
    u64 phys = b->ecam_base + ((u64)bus << 20) + ((u64)dev << 15) + ((u64)fn << 12) + (u64)off;
    return (volatile u8 *)Mm_PhysToVirt(phys);
}

u8 Croi_Pci_Read8(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off)
{
    volatile u8 *p = ecam_addr(b, bus, dev, fn, off);
    return p ? *p : 0xFFu;
}

u16 Croi_Pci_Read16(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off)
{
    volatile u8 *p = ecam_addr(b, bus, dev, fn, off);
    if (!p) {
        return 0xFFFFu;
    }
    // Naturally-aligned 16-bit access; offsets in the PCI header are
    // 2-byte aligned for 16-bit fields per PCI 3.0 §6.1.
    return *(volatile u16 *)p;
}

u32 Croi_Pci_Read32(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off)
{
    volatile u8 *p = ecam_addr(b, bus, dev, fn, off);
    if (!p) {
        return 0xFFFFFFFFu;
    }
    return *(volatile u32 *)p;
}

void Croi_Pci_Write8(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off, u8 v)
{
    volatile u8 *p = ecam_addr(b, bus, dev, fn, off);
    if (p) {
        *p = v;
    }
}

void Croi_Pci_Write16(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off, u16 v)
{
    volatile u8 *p = ecam_addr(b, bus, dev, fn, off);
    if (p) {
        *(volatile u16 *)p = v;
    }
}

void Croi_Pci_Write32(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn, u16 off, u32 v)
{
    volatile u8 *p = ecam_addr(b, bus, dev, fn, off);
    if (p) {
        *(volatile u32 *)p = v;
    }
}
