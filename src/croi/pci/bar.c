// SPDX-License-Identifier: BSD-2-Clause
//
// PCI BAR sizing + allocation. PCI 3.0 §6.2.5.1 specifies the BAR
// header bits and the write-1s read-back sizing trick:
//
//   For Memory BARs (bit 0 = 0):
//     bit 0    : 0 (memory)
//     bits 1-2 : 00 = 32-bit, 10 = 64-bit, 11 = reserved
//     bit 3    : prefetchable
//     bits 4+  : base address (low bits zero according to size)
//
// To size: write 0xFFFFFFFF to the BAR register, read back, mask
// off the type bits, invert and add 1 → size in bytes. For 64-bit
// BARs the same trick is applied to BAR[index] AND BAR[index+1] for
// the upper 32 bits.
//
// CaraOS Phase 1 only allocates from the host bridge's MEM32 range.
// 64-bit BARs are programmed with their upper 32 bits zero (any
// MEM32 address fits a 64-bit BAR's value field). The xHCI controller
// + every Phase 5 device we plan to support has a small enough
// register window for this; full 64-bit allocation arrives when a
// device actually needs >4 GiB of register space.

#include <cara/log.h>
#include <cara/pci.h>
#include <cara/types.h>

static u64 align_up(u64 v, u64 a)
{
    if (a == 0) {
        return v;
    }
    return (v + a - 1) & ~(a - 1);
}

[[nodiscard]] int Croi_Pci_AllocateBar(struct PciInventory *inv,
                                       u32 func_index, u32 bar_index)
{
    if (!inv || func_index >= inv->n_funcs || bar_index >= CARA_MAX_PCI_BARS) {
        return CARA_EINVAL;
    }
    struct PciFunction *fn = &inv->func[func_index];
    u8 bus = fn->bus, dev = fn->device, fnnum = fn->function;
    u16 bar_off = (u16)(PCI_CFG_BAR0 + bar_index * 4);

    // Save the original BAR (and its upper half if 64-bit).
    u32 orig_lo = Croi_Pci_Read32(&inv->bridge, bus, dev, fnnum, bar_off);
    u32 orig_hi = 0;

    if (orig_lo & 1u) {
        // I/O BAR — Phase 1 doesn't allocate I/O.
        return CARA_EINVAL;
    }
    u8 type = (u8)((orig_lo >> 1) & 0x3u);
    bool is_64 = (type == 0x2);
    bool prefetchable = ((orig_lo >> 3) & 1u) != 0;
    if (type != 0x0 && type != 0x2) {
        // type 0x1 = "must be located below 1 MiB" (legacy); 0x3 reserved.
        return CARA_EINVAL;
    }
    if (is_64 && bar_index + 1 >= CARA_MAX_PCI_BARS) {
        return CARA_EINVAL;
    }
    if (is_64) {
        orig_hi = Croi_Pci_Read32(&inv->bridge, bus, dev, fnnum,
                                  (u16)(bar_off + 4));
    }

    // Probe size: write 1s to both halves, read back, restore.
    Croi_Pci_Write32(&inv->bridge, bus, dev, fnnum, bar_off, 0xFFFFFFFFu);
    u32 sz_lo = Croi_Pci_Read32(&inv->bridge, bus, dev, fnnum, bar_off);
    u32 sz_hi = 0;
    if (is_64) {
        Croi_Pci_Write32(&inv->bridge, bus, dev, fnnum,
                         (u16)(bar_off + 4), 0xFFFFFFFFu);
        sz_hi = Croi_Pci_Read32(&inv->bridge, bus, dev, fnnum,
                                (u16)(bar_off + 4));
    }
    // Restore original (we'll overwrite with the allocated address below).
    Croi_Pci_Write32(&inv->bridge, bus, dev, fnnum, bar_off, orig_lo);
    if (is_64) {
        Croi_Pci_Write32(&inv->bridge, bus, dev, fnnum,
                         (u16)(bar_off + 4), orig_hi);
    }

    // Compute size. Mask off type bits, invert, add 1.
    u64 sz_raw = ((u64)sz_hi << 32) | (u64)sz_lo;
    sz_raw &= ~0xFull;
    if (sz_raw == 0) {
        // BAR not implemented.
        return CARA_ENOENT;
    }
    u64 size = (~sz_raw) + 1;

    // Bump-allocate from the MEM32 range, naturally aligned.
    u64 base = align_up(inv->mem32_cursor, size);
    if (base + size > inv->mem32_end || base < inv->mem32_cursor) {
        return CARA_ENOMEM;
    }
    inv->mem32_cursor = base + size;

    // Write the allocated base into the BAR. Preserve the type bits
    // (bits 0..3) of the original value.
    u32 type_bits = orig_lo & 0xFu;
    Croi_Pci_Write32(&inv->bridge, bus, dev, fnnum, bar_off,
                     (u32)((base & 0xFFFFFFF0u) | type_bits));
    if (is_64) {
        Croi_Pci_Write32(&inv->bridge, bus, dev, fnnum,
                         (u16)(bar_off + 4), (u32)(base >> 32));
    }

    // Enable Memory Space + Bus Master decoding.
    u16 cmd = Croi_Pci_Read16(&inv->bridge, bus, dev, fnnum, PCI_CFG_COMMAND);
    cmd |= (u16)(PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER);
    Croi_Pci_Write16(&inv->bridge, bus, dev, fnnum, PCI_CFG_COMMAND, cmd);

    // Stash in the function's bar[] record.
    fn->bar[bar_index] = (struct PciBar){
        .kind         = is_64 ? PCI_RANGE_MEM64 : PCI_RANGE_MEM32,
        .prefetchable = prefetchable,
        .base         = base,
        .size         = size,
    };
    if (is_64) {
        // Mark the consumed upper-half slot so callers don't double-allocate.
        fn->bar[bar_index + 1] = (struct PciBar){ 0 };
    }

    // Read back to confirm the BAR write actually took (some
    // platforms ignore writes outside the bridge's ranges).
    u32 readback_lo = Croi_Pci_Read32(&inv->bridge, bus, dev, fnnum, bar_off);
    u32 readback_hi = is_64 ? Croi_Pci_Read32(&inv->bridge, bus, dev, fnnum,
                                              (u16)(bar_off + 4))
                            : 0;
    u16 cmd_after = Croi_Pci_Read16(&inv->bridge, bus, dev, fnnum,
                                    PCI_CFG_COMMAND);
    LOG_INFO("pci ",
             "alloc BAR%u for %x: base=0x%llx size=0x%llx %s readback=%x:%x cmd=%x",
             (unsigned)bar_index,
             ((u32)bus << 16) | ((u32)dev << 8) | fnnum,
             (u64)base, (u64)size,
             is_64 ? "(64-bit MEM)" : "(32-bit MEM)",
             readback_hi, readback_lo, (unsigned)cmd_after);
    return CARA_EOK;
}
