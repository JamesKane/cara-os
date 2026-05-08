// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(pci_smoke): asserts the boot-time PCIe enumeration
// (Croi_Pci_Init in entry.c) discovered QEMU virt's
// `pci-host-ecam-generic` host bridge with the expected ECAM base
// (0x3000_0000) and bus-range. With `-device qemu-xhci` on the
// QEMU command line, also asserts an xHCI function (class
// 0x0C / 0x03 / 0x30) appears in the inventory.

#include <cara/log.h>
#include <cara/pci.h>
#include <cara/test.h>
#include <cara/types.h>

extern struct PciInventory g_pci_inv;
extern bool                g_pci_inited;

KERNEL_TEST(pci_smoke)
{
    TEST_ASSERT(ctx, g_pci_inited,
                "PCIe init did not run successfully at boot");

    // QEMU virt's pci-host-ecam-generic puts ECAM at 0x3000_0000.
    // The X1 places its ECAM elsewhere; this assertion is the
    // expected QEMU-virt value.
    TEST_ASSERT(ctx, g_pci_inv.bridge.ecam_base == 0x30000000ull,
                "ECAM base != 0x30000000 (QEMU virt expected)");

    TEST_ASSERT(ctx, g_pci_inv.bridge.bus_first == 0,
                "bus_first != 0");
    TEST_ASSERT(ctx, g_pci_inv.bridge.bus_last >= 0,
                "bus_last invalid");
    TEST_ASSERT(ctx, g_pci_inv.bridge.n_ranges > 0,
                "host bridge has no ranges");

    // qemu-xhci, when attached via `-device qemu-xhci`, advertises
    // class 0x0C subclass 0x03 prog_if 0x30. The smoke harness
    // adds that device to its QEMU command line; if the test runs
    // without it (e.g. plain -M virt), the assertion below is the
    // signal that the harness needs updating.
    u32 cursor = 0;
    const struct PciFunction *xhci = Croi_Pci_FindByClass(
        &g_pci_inv,
        PCI_CLASS_USB_XHCI_BASE,
        PCI_CLASS_USB_XHCI_SUB,
        PCI_CLASS_USB_XHCI_PROGIF,
        &cursor);
    TEST_ASSERT(ctx, xhci != nullptr,
                "no xHCI controller found — is -device qemu-xhci on the QEMU cmdline?");

    LOG_INFO("pcism", "xHCI at %02x:%02x.%u  %04x:%04x",
             (unsigned)xhci->bus, (unsigned)xhci->device,
             (unsigned)xhci->function,
             (unsigned)xhci->vendor_id, (unsigned)xhci->device_id);
}
