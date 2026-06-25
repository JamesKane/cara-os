// SPDX-License-Identifier: BSD-2-Clause
//
// RISC-V (Sv39) MMU privileged ops — the arch_mmu_* seam (epic H, H.2). The
// PTE encoding + the generic 3-level/4 KiB walk are arch-neutral (see
// cara/arch_pte.h + src/croi/mm/pt.c); these are the parts that touch the
// live MMU: activate (satp + sfence), fence, and reading the active root.

#include <cara/arch.h>
#include <cara/mm.h> // struct PageTable, Sv39_Satp, Mm_PhysToVirt
#include <cara/types.h>

void arch_mmu_activate(const struct PageTable *pt)
{
    u64 satp = Sv39_Satp(pt);
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
    __asm__ volatile("csrw satp, %0" : : "r"(satp) : "memory");
    __asm__ volatile("sfence.vma zero, zero" ::: "memory");
}

void arch_mmu_fence(void)
{
    __asm__ volatile("sfence.vma" ::: "memory");
}

void arch_mmu_fence_va(u64 va)
{
    __asm__ volatile("sfence.vma %0, x0" : : "r"(va) : "memory");
}

u64 *arch_mmu_boot_root(void)
{
    u64 satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    u64 ppn = satp & ((1ull << 44) - 1);
    return (u64 *)Mm_PhysToVirt(ppn << 12);
}
