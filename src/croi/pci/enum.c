// SPDX-License-Identifier: BSD-2-Clause
//
// PCIe bus walk + per-function inventory.
//
// Discovers the host bridge from the FDT, then walks every (bus,
// device, function) tuple in the bridge's bus-range looking for
// live functions. Per function, reads VID / DID / class triple /
// header type into a `struct PciFunction`. BAR sizing and
// allocation are deferred — the xHCI driver (Tier 1 Epic UB) does
// that against a function it gets back from Croi_Pci_FindByClass.
//
// PCI-to-PCI bridges (header type 0x01) currently log "found
// bridge; nested-bus walk deferred" and skip recursion. QEMU virt
// + the X1 both expose xHCI directly off bus 0; nested bridges
// are a Phase 5 concern (USB hubs aren't PCI-bridge-shaped).

#include <cara/fdt.h>
#include <cara/log.h>
#include <cara/pci.h>
#include <cara/types.h>

static void
classify_function(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn,
                  struct PciFunction *out)
{
    *out = (struct PciFunction){
        .bus       = bus,
        .device    = dev,
        .function  = fn,
        .vendor_id = Croi_Pci_Read16(b, bus, dev, fn, PCI_CFG_VENDOR_ID),
        .device_id = Croi_Pci_Read16(b, bus, dev, fn, PCI_CFG_DEVICE_ID),
        .revision  = Croi_Pci_Read8 (b, bus, dev, fn, PCI_CFG_REVISION),
        .prog_if   = Croi_Pci_Read8 (b, bus, dev, fn, PCI_CFG_PROG_IF),
        .subclass  = Croi_Pci_Read8 (b, bus, dev, fn, PCI_CFG_SUBCLASS),
        .base_class= Croi_Pci_Read8 (b, bus, dev, fn, PCI_CFG_BASE_CLASS),
    };
    u8 ht = Croi_Pci_Read8(b, bus, dev, fn, PCI_CFG_HEADER_TYPE);
    out->header_type   = ht & 0x7Fu;
    out->multifunction = (ht & PCI_HEADER_TYPE_MULTI) != 0;
}

static bool function_present(const struct PciHostBridge *b, u8 bus, u8 dev, u8 fn)
{
    return Croi_Pci_Read16(b, bus, dev, fn, PCI_CFG_VENDOR_ID) != 0xFFFFu;
}

[[nodiscard]] int Croi_Pci_Init(struct PciInventory *out, const struct Fdt *fdt)
{
    if (!out || !fdt) {
        return CARA_EINVAL;
    }
    *out = (struct PciInventory){ 0 };

    int rc = Croi_Pci_HostBridgeFromFdt(&out->bridge, fdt);
    if (rc != CARA_EOK) {
        return rc;
    }
    LOG_INFO("pci ",
             "host bridge: ECAM 0x%llx (%llu MiB), bus [%u..%u], %u ranges",
             (u64)out->bridge.ecam_base,
             (u64)(out->bridge.ecam_size >> 20),
             (unsigned)out->bridge.bus_first,
             (unsigned)out->bridge.bus_last,
             (unsigned)out->bridge.n_ranges);

    for (u32 bus = out->bridge.bus_first;
         bus <= out->bridge.bus_last && out->n_funcs < CARA_MAX_PCI_FUNCTIONS;
         bus++) {
        for (u8 dev = 0;
             dev < 32 && out->n_funcs < CARA_MAX_PCI_FUNCTIONS;
             dev++) {
            // Probe function 0 first — if absent, skip the whole device
            // (multi-function devices must populate fn 0). Per PCI 3.0 §6.2.
            if (!function_present(&out->bridge, (u8)bus, dev, 0)) {
                continue;
            }
            struct PciFunction f0;
            classify_function(&out->bridge, (u8)bus, dev, 0, &f0);
            out->func[out->n_funcs++] = f0;
            if (f0.header_type == PCI_HEADER_TYPE_BRIDGE) {
                LOG_DEBUG("pci ",
                          "bridge at %02x:%02x.0 — nested-bus walk deferred",
                          (unsigned)bus, (unsigned)dev);
            }

            if (!f0.multifunction) {
                continue;
            }
            for (u8 fn = 1;
                 fn < 8 && out->n_funcs < CARA_MAX_PCI_FUNCTIONS;
                 fn++) {
                if (!function_present(&out->bridge, (u8)bus, dev, fn)) {
                    continue;
                }
                struct PciFunction fx;
                classify_function(&out->bridge, (u8)bus, dev, fn, &fx);
                out->func[out->n_funcs++] = fx;
            }
        }
    }

    LOG_INFO("pci ", "enumerated %u function(s) across bus [%u..%u]",
             (unsigned)out->n_funcs,
             (unsigned)out->bridge.bus_first,
             (unsigned)out->bridge.bus_last);
    for (u32 i = 0; i < out->n_funcs; i++) {
        const struct PciFunction *f = &out->func[i];
        // Kernel printf doesn't grok %02x width modifiers; encode the
        // bus/device/function and class triple as packed u32s and let
        // the caller decode by sight.
        u32 bdf      = ((u32)f->bus << 16) | ((u32)f->device << 8) | f->function;
        u32 vid_did  = ((u32)f->vendor_id << 16) | f->device_id;
        u32 class_tr = ((u32)f->base_class << 16) | ((u32)f->subclass << 8)
                       | f->prog_if;
        LOG_INFO("pci ", "  bdf=%x vid:did=%x class=%x hdr=%x",
                 bdf, vid_did, class_tr, (unsigned)f->header_type);
    }
    return CARA_EOK;
}

const struct PciFunction *
Croi_Pci_FindByClass(const struct PciInventory *inv, u8 base_class,
                     u8 subclass, u8 prog_if, u32 *cursor_inout)
{
    if (!inv || !cursor_inout) {
        return nullptr;
    }
    for (u32 i = *cursor_inout; i < inv->n_funcs; i++) {
        const struct PciFunction *f = &inv->func[i];
        if (f->base_class == base_class
            && f->subclass == subclass
            && f->prog_if  == prog_if) {
            *cursor_inout = i + 1;
            return f;
        }
    }
    *cursor_inout = inv->n_funcs;
    return nullptr;
}
