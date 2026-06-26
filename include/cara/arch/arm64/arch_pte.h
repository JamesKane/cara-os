// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 stage-1 page-table descriptor encoding — one of the CARA_ARCH-
// selected variants behind <cara/arch_pte.h> (epic H.7.2b). Pure bit-math
// `static inline`s (no privileged instructions) so cara_mm compiles for the
// AArch64 kernel. 4 KiB granule, 39-bit VA (T0SZ=T1SZ=25), 3-level walk —
// chosen so the generic 3-level / 9-bit-index walk in src/croi/mm/pt.c is
// shared with Sv39. The privileged side (TTBR/TLBI) lives in
// src/croi/arch/arm64/mmu.c behind arch_mmu_* (cara/arch.h).
//
// ⚠ ONE OPEN SEAM (reconciled when Page_Map actually runs on AArch64 — H.7.4,
// with the scheduler / user address spaces). AArch64 encodes leaf-vs-table BY
// LEVEL, not by the descriptor bits alone:
//   - L0/L1/L2: bits[1:0] = 0b01 → block (a leaf); 0b11 → table (points down)
//   - L3:       bits[1:0] = 0b11 → page (a leaf)   ; 0b01 → reserved/invalid
// So a page (L3 leaf) and a table (L1/L2) share bits 0b11 and are only told
// apart by the level. The generic walk currently asks arch_pte_is_leaf(pte)
// with no level (a bits-only test that works for Sv39 and for AArch64 L1/L2
// blocks but is ambiguous at L3). Until H.7.4 teaches the walk the level (e.g.
// arch_pte_is_table(pte, level)), Page_Map / Croi_NewKernelPT are NOT yet
// correct on AArch64 — and nothing calls them here: H.7.2b uses only the page
// *allocator* + physmap (which never touch this encoding). The boot page tables
// are hand-built block descriptors in arch/arm64/_start.S, not this walk.

#ifndef CARA_ARCH_ARM64_PTE_H
#define CARA_ARCH_ARM64_PTE_H

#include <cara/types.h>

// Descriptor type bits [1:0].
enum : u64 {
    PTE_V = 0x1ull,          // valid (bit 0)
    PTE_TYPE_TABLE = 0x2ull, // bit 1: table (L0–L2) or page (L3); clear ⇒ block
};

// Stage-1 lower attributes [11:2] / upper attributes [54:53].
enum : u64 {
    PTE_ATTRIDX_DEVICE = (0ull << 2), // MAIR Attr0 = device-nGnRnE
    PTE_ATTRIDX_NORMAL = (1ull << 2), // MAIR Attr1 = normal write-back
    PTE_NS = (1ull << 5),
    PTE_AP_RW_EL1 = (0ull << 6), // EL1 RW, EL0 none
    PTE_AP_RW_ALL = (1ull << 6), // EL1 RW, EL0 RW
    PTE_AP_RO_EL1 = (2ull << 6), // EL1 RO, EL0 none
    PTE_AP_RO_ALL = (3ull << 6), // EL1 RO, EL0 RO
    PTE_SH_INNER = (3ull << 8),  // inner-shareable
    PTE_AF = (1ull << 10),       // access flag
    PTE_NG = (1ull << 11),       // non-global (per-ASID; user mappings)
    PTE_PXN = (1ull << 53),      // privileged execute-never
    PTE_UXN = (1ull << 54),      // unprivileged execute-never
};

// Pre-composed protection masks. Same names the portable callers (pt.c) use;
// the AArch64 bit pattern differs from Sv39. Kernel mappings are global
// (no nG); user mappings set nG. (See the L3-vs-block caveat at the top: these
// are validated when Page_Map runs on AArch64 in H.7.4.)
constexpr u64 PTE_KERNEL_RWX =
    (PTE_V | PTE_ATTRIDX_NORMAL | PTE_AP_RW_EL1 | PTE_SH_INNER | PTE_AF | PTE_UXN);
constexpr u64 PTE_KERNEL_RW =
    (PTE_V | PTE_ATTRIDX_NORMAL | PTE_AP_RW_EL1 | PTE_SH_INNER | PTE_AF | PTE_UXN | PTE_PXN);
constexpr u64 PTE_USER_RW = (PTE_V | PTE_ATTRIDX_NORMAL | PTE_AP_RW_ALL | PTE_SH_INNER | PTE_AF |
                             PTE_UXN | PTE_PXN | PTE_NG);
constexpr u64 PTE_USER_RX =
    (PTE_V | PTE_ATTRIDX_NORMAL | PTE_AP_RO_ALL | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_NG);
constexpr u64 PTE_USER_RO = (PTE_V | PTE_ATTRIDX_NORMAL | PTE_AP_RO_ALL | PTE_SH_INNER | PTE_AF |
                             PTE_UXN | PTE_PXN | PTE_NG);
constexpr u64 PTE_USER_RWX =
    (PTE_V | PTE_ATTRIDX_NORMAL | PTE_AP_RW_ALL | PTE_SH_INNER | PTE_AF | PTE_PXN | PTE_NG);

// Output-address field for a 4 KiB descriptor: PA[47:12].
constexpr u64 PTE_OA_MASK = 0x0000FFFFFFFFF000ull;

// Leaf descriptor. NB: encoded as an L3 *page* (bits[1:0]=0b11); a 1 GiB/2 MiB
// *block* leaf at L1/L2 would be 0b01. The walk's leaf level is L3, so this is
// right there; block leaves are produced directly by the boot asm.
static inline u64 arch_pte_leaf(u64 pa, u64 prot)
{
    return (pa & PTE_OA_MASK) | prot | PTE_TYPE_TABLE; // 0b11 page + attrs
}

// Table descriptor pointing at a child table (bits[1:0]=0b11, no attrs).
static inline u64 arch_pte_table(u64 child_pa)
{
    return (child_pa & PTE_OA_MASK) | PTE_V | PTE_TYPE_TABLE;
}

static inline bool arch_pte_valid(u64 pte)
{
    return (pte & PTE_V) != 0;
}

// Bits-only leaf test: true for an L1/L2 *block* (0b01). NOT valid at L3 (a
// page is 0b11, indistinguishable from a table by bits alone) — see the
// top-of-file caveat; the level-aware walk lands in H.7.4.
static inline bool arch_pte_is_leaf(u64 pte)
{
    return (pte & PTE_TYPE_TABLE) == 0; // block descriptor (L1/L2)
}

// Physical address of the child table (or output page) a descriptor points at.
static inline u64 arch_pte_child_pa(u64 pte)
{
    return pte & PTE_OA_MASK;
}

#endif // CARA_ARCH_ARM64_PTE_H
