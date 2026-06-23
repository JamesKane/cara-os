// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(expansion_configdev): expansion.library's FDT-backed
// AutoConfig (L14.1) — the boot PCIe inventory surfaced as a ConfigDev
// list. Walks the list via FindConfigDev, checks the per-board count + the
// PciFunction → ConfigDev field mapping, and exercises Alloc/Add/Rem/Free
// on a synthetic board. Population reads g_pci_inv directly (no Process);
// needs the QEMU PCI host + devices the smoke harness provides (same
// caveat as the pci/nvme tests).

#include <cara/expansion_lib.h>
#include <cara/pci.h>
#include <cara/test.h>
#include <cara/types.h>
#include <libraries/configvars.h>

extern struct PciInventory g_pci_inv;
extern bool g_pci_inited;

KERNEL_TEST(expansion_configdev)
{
    TEST_ASSERT(ctx, g_pci_inited, "PCI inited (no pci host on the qemu line?)");

    // The list walk visits one ConfigDev per discovered PCI function.
    u32 count = 0;
    for (struct ConfigDev *cd = Croi_Exp_FindConfigDev_Impl(nullptr, -1, -1); cd;
         cd = Croi_Exp_FindConfigDev_Impl(cd, -1, -1)) {
        count++;
    }
    TEST_ASSERT(ctx, count == g_pci_inv.n_funcs, "one ConfigDev per PCI function");
    TEST_ASSERT(ctx, count > 0, "at least one board enumerated");

    // The first board maps back to g_pci_inv.func[0].
    const struct PciFunction *f = &g_pci_inv.func[0];
    struct ConfigDev *d = Croi_Exp_FindConfigDev_Impl(nullptr, (LONG)f->vendor_id,
                                                      (LONG)(f->device_id & 0xFFu));
    TEST_ASSERT(ctx, d != nullptr, "find first board by vendor/product");
    TEST_ASSERT(ctx, d->cd_Rom.er_Manufacturer == f->vendor_id, "er_Manufacturer = vendor_id");
    TEST_ASSERT(ctx, d->cd_Rom.er_Product == (UBYTE)(f->device_id & 0xFFu),
                "er_Product = device_id low byte");
    TEST_ASSERT(ctx, d->cd_BoardAddr == (APTR)(uptr)f->bar[0].base, "cd_BoardAddr = bar0 base");
    TEST_ASSERT(ctx, d->cd_BoardSize == (ULONG)f->bar[0].size, "cd_BoardSize = bar0 size");

    // Alloc/Add/Find/Rem/Free a synthetic board (a clearly-fake id).
    struct ConfigDev *s = Croi_Exp_AllocConfigDev_Impl();
    TEST_ASSERT(ctx, s != nullptr, "AllocConfigDev");
    s->cd_Rom.er_Manufacturer = 0x9999;
    s->cd_Rom.er_Product = 0x99;
    Croi_Exp_AddConfigDev_Impl(s);
    TEST_ASSERT(ctx, Croi_Exp_FindConfigDev_Impl(nullptr, 0x9999, 0x99) == s, "find synthetic");
    Croi_Exp_RemConfigDev_Impl(s);
    TEST_ASSERT(ctx, Croi_Exp_FindConfigDev_Impl(nullptr, 0x9999, 0x99) == nullptr,
                "removed from list");
    Croi_Exp_FreeConfigDev_Impl(s);
}
