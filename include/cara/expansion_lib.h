// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ expansion.library LVOs Croi
// serves (docs/EXPANSION.md). expansion is `syscall` flavour: each LVO
// reaches src/croi/syscall/syscall.c via its trampoline (Cara_Trampoline_
// Exp_* in src/croi/expansion/trampolines.S), routed to the Croi_Exp_*_Impl
// below. L14.1 ships the ConfigDev list ops; the list is the FDT/PCI boot
// inventory (g_pci_inv) presented as ConfigDevs — built lazily on first
// access. The Zorro-config / expansion-mem / binding / boot-node LVOs are
// stubbed (no Zorro bus; CaraFS mounts over NVMe at boot).

#ifndef CARA_EXPANSION_LIB_H
#define CARA_EXPANSION_LIB_H

#include <cara/types.h>
#include <exec/types.h>
#include <libraries/configvars.h>

struct Library;

// ---- Reserved-slot library hooks (`local` flavour) ------------------
struct Library *Croi_Exp_Open(struct Library *base, struct ExpansionBase *eb);
void Croi_Exp_Close(struct Library *base, struct ExpansionBase *eb);
void Croi_Exp_Expunge(struct Library *base, struct ExpansionBase *eb);
ULONG Croi_Exp_ExtFunc(struct Library *base, struct ExpansionBase *eb);

// ---- ConfigDev list (L14.1) -----------------------------------------
struct ConfigDev *Croi_Exp_AllocConfigDev_Impl(void);
void Croi_Exp_FreeConfigDev_Impl(struct ConfigDev *cd);
void Croi_Exp_AddConfigDev_Impl(struct ConfigDev *cd);
void Croi_Exp_RemConfigDev_Impl(struct ConfigDev *cd);
// FindConfigDev: first board after `oldConfigDev` (or from the list head if
// null) whose er_Manufacturer/er_Product match; -1 is the wildcard.
struct ConfigDev *Croi_Exp_FindConfigDev_Impl(struct ConfigDev *oldConfigDev, LONG manufacturer,
                                              LONG product);

#endif // CARA_EXPANSION_LIB_H
