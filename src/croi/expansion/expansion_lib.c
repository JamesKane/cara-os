// SPDX-License-Identifier: BSD-2-Clause
//
// expansion.library ConfigDev list (L14.1, docs/EXPANSION.md). CaraOS has
// no Zorro bus, so the FDT/PCI boot inventory (g_pci_inv, filled by
// Croi_Pci_Init) IS the AutoConfig chain: each PciFunction is presented as
// a ConfigDev. The list is built lazily on first access and lives on the
// SASOS shared heap; FindConfigDev walks it. The Zorro board-config /
// expansion-memory / binding / DOS-boot-node LVOs are stubbed.

#include <cara/alloc.h>
#include <cara/expansion_lib.h>
#include <cara/pci.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <exec/types.h>
#include <libraries/configvars.h>

// The boot-time PCIe inventory (entry.c globals).
extern struct PciInventory g_pci_inv;
extern bool g_pci_inited;

// The system ConfigDev list + a one-time populate flag.
static struct ConfigDev *g_exp_head = nullptr;
static bool g_exp_populated = false;

struct ConfigDev *Croi_Exp_AllocConfigDev_Impl(void)
{
    struct ConfigDev *cd = (struct ConfigDev *)Croi_AllocShared(sizeof *cd);
    if (cd) {
        *cd = (struct ConfigDev){ 0 };
    }
    return cd;
}

void Croi_Exp_FreeConfigDev_Impl(struct ConfigDev *cd)
{
    if (cd) {
        Croi_Free(cd);
    }
}

void Croi_Exp_AddConfigDev_Impl(struct ConfigDev *cd)
{
    if (!cd) {
        return;
    }
    cd->cd_NextCD = nullptr;
    if (!g_exp_head) {
        g_exp_head = cd;
        return;
    }
    struct ConfigDev *t = g_exp_head;
    while (t->cd_NextCD) {
        t = t->cd_NextCD;
    }
    t->cd_NextCD = cd;
}

void Croi_Exp_RemConfigDev_Impl(struct ConfigDev *cd)
{
    if (!cd) {
        return;
    }
    struct ConfigDev **pp = &g_exp_head;
    while (*pp && *pp != cd) {
        pp = &(*pp)->cd_NextCD;
    }
    if (*pp == cd) {
        *pp = cd->cd_NextCD;
    }
    cd->cd_NextCD = nullptr;
}

// Present the boot PCIe inventory as ConfigDevs, once.
static void exp_ensure_populated(void)
{
    if (g_exp_populated) {
        return;
    }
    g_exp_populated = true;
    if (!g_pci_inited) {
        return;
    }
    for (u32 i = 0; i < g_pci_inv.n_funcs; i++) {
        const struct PciFunction *f = &g_pci_inv.func[i];
        struct ConfigDev *cd = Croi_Exp_AllocConfigDev_Impl();
        if (!cd) {
            break;
        }
        cd->cd_Flags = CDF_PROCESSED; // already configured by the BAR allocator
        cd->cd_Rom.er_Type = ERT_NEWBOARD;
        cd->cd_Rom.er_Manufacturer = f->vendor_id;
        cd->cd_Rom.er_Product = (UBYTE)(f->device_id & 0xFFu);
        cd->cd_BoardAddr = (APTR)(uptr)f->bar[0].base;
        cd->cd_BoardSize = (ULONG)f->bar[0].size;
        Croi_Exp_AddConfigDev_Impl(cd);
    }
}

struct ConfigDev *Croi_Exp_FindConfigDev_Impl(struct ConfigDev *oldConfigDev, LONG manufacturer,
                                              LONG product)
{
    exp_ensure_populated();
    struct ConfigDev *cd = oldConfigDev ? oldConfigDev->cd_NextCD : g_exp_head;
    for (; cd; cd = cd->cd_NextCD) {
        if (manufacturer != FINDCONFIGDEV_ANY &&
            cd->cd_Rom.er_Manufacturer != (UWORD)manufacturer) {
            continue;
        }
        if (product != FINDCONFIGDEV_ANY && cd->cd_Rom.er_Product != (UBYTE)product) {
            continue;
        }
        return cd;
    }
    return nullptr;
}
