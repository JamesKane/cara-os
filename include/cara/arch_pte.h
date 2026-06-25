// SPDX-License-Identifier: BSD-2-Clause
//
// cara/arch_pte.h — the architecture's page-table-entry encoding (epic H,
// H.2). These are pure bit-math `static inline`s with no privileged
// instructions, so they compile for both the host (cara_mm unit tests) and
// the kernel; the generic page-table walk in src/croi/mm/pt.c uses them so
// the walk itself stays arch-neutral. The privileged side of the MMU (the
// satp/sfence activate + fence + boot-root read) lives in src/croi/arch/
// <arch>/mmu.c behind arch_mmu_* (declared in cara/arch.h).
//
// This is the RISC-V Sv39 encoding. A second arch (ARM64, H.7) supplies its
// own arch_pte.h (AArch64 stage-1 descriptors) selected by the build.

#ifndef CARA_ARCH_PTE_H
#define CARA_ARCH_PTE_H

#include <cara/types.h>

// Sv39 PTE flag bits ([9:0]).
enum : u64 {
    PTE_V = 0x001ull,
    PTE_R = 0x002ull,
    PTE_W = 0x004ull,
    PTE_X = 0x008ull,
    PTE_U = 0x010ull, // user-accessible
    PTE_G = 0x020ull, // global mapping (kernel half)
    PTE_A = 0x040ull,
    PTE_D = 0x080ull,
};

// Pre-composed protection masks (the `prot` argument to Page_Map / the
// boot-leaf install). Arch-specific values; portable callers pass these
// names, not raw bits.
constexpr u64 PTE_KERNEL_RWX = (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D);
constexpr u64 PTE_KERNEL_RW = (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D);
constexpr u64 PTE_USER_RW = (PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
constexpr u64 PTE_USER_RX = (PTE_V | PTE_R | PTE_X | PTE_U | PTE_A);
constexpr u64 PTE_USER_RO = (PTE_V | PTE_R | PTE_U | PTE_A);
constexpr u64 PTE_USER_RWX = (PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D);

// Leaf PTE: physical page number in [53:10], flags in [9:0].
static inline u64 arch_pte_leaf(u64 pa, u64 prot)
{
    return ((pa >> 12) << 10) | prot;
}

// Non-leaf (table) PTE pointing at a child table: V set, no R/W/X.
static inline u64 arch_pte_table(u64 child_pa)
{
    return ((child_pa >> 12) << 10) | PTE_V;
}

static inline bool arch_pte_valid(u64 pte)
{
    return (pte & PTE_V) != 0;
}

// A valid PTE with any of R/W/X is a leaf; with none it points at a table.
static inline bool arch_pte_is_leaf(u64 pte)
{
    return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}

// Physical address of the child table (or leaf page) a PTE points at.
static inline u64 arch_pte_child_pa(u64 pte)
{
    return ((pte >> 10) & 0x0FFFFFFFFFFFull) << 12;
}

#endif // CARA_ARCH_PTE_H
