// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 MMU privileged ops — the arch_mmu_* seam (epic H.7.4a, docs/ARM64.md
// §3). The PTE encoding + the generic 3-level/4 KiB walk are arch-neutral
// (cara/arch/arm64/arch_pte.h + src/croi/mm/pt.c); these are the parts that
// touch the live MMU.
//
// Unlike RISC-V (one satp covering both halves), AArch64 splits translation:
// TTBR1_EL1 holds the fixed kernel upper half (set up once in _start.S), and
// TTBR0_EL1 holds the per-task user/low half. So arch_mmu_activate only swaps
// TTBR0 — the kernel keeps executing out of TTBR1, unaffected by the switch.
// Translations are ASID-tagged (TCR_EL1.A1=0 ⇒ ASID from TTBR0), so a fresh
// ASID needs no TLB flush.

#include <cara/arch.h>
#include <cara/mm.h> // struct PageTable, Mm_PhysToVirt / Mm_VirtToPhys
#include <cara/types.h>

// Output-address field of a translation-table base register: PA[47:12].
#define TTBR_BADDR_MASK 0x0000FFFFFFFFF000ull

void arch_mmu_activate(const struct PageTable *pt)
{
    // TTBR0_EL1 = root PA in [47:1], ASID in [63:48]. The user root is a
    // direct-map (upper-half) pointer, so convert back to physical.
    u64 ttbr0 = (Mm_VirtToPhys(pt->root) & TTBR_BADDR_MASK) | ((u64)pt->asid << 48);
    __asm__ volatile("dsb ish" ::: "memory");
    __asm__ volatile("msr ttbr0_el1, %0" : : "r"(ttbr0) : "memory");
    __asm__ volatile("isb" ::: "memory");
}

void arch_mmu_fence(void)
{
    // Flush the whole EL1&0 stage-1 TLB (all ASIDs). Used for boot-time
    // mapping changes.
    __asm__ volatile("dsb ishst\n\ttlbi vmalle1\n\tdsb ish\n\tisb" ::: "memory");
}

void arch_mmu_fence_va(u64 va)
{
    // Invalidate the stage-1 EL1&0 entry for a single VA (all ASIDs). The
    // TLBI VAAE1 operand is VA[55:12] in bits [43:0].
    u64 page = (va >> 12) & 0xFFFFFFFFFFFull;
    __asm__ volatile("dsb ishst\n\ttlbi vaae1, %0\n\tdsb ish\n\tisb" : : "r"(page) : "memory");
}

u64 *arch_mmu_boot_root(void)
{
    // The kernel half lives in TTBR1; return its root as an upper-half VA.
    // (On AArch64 the boot-leaf install path — Croi_Mm_InstallBootPT_1GiBLeaf —
    // is RISC-V-only, so nothing calls this yet; provided for the seam.)
    u64 ttbr1;
    __asm__ volatile("mrs %0, ttbr1_el1" : "=r"(ttbr1));
    return (u64 *)Mm_PhysToVirt(ttbr1 & TTBR_BADDR_MASK);
}
