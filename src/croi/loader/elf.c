// SPDX-License-Identifier: BSD-2-Clause
//
// ELF64 loader for static RISC-V user binaries. Walks PT_LOAD
// segments, allocates user pages, copies file bytes, zero-fills the
// .bss tail, and Page_Maps each page with PTE_USER_ flags derived
// from p_flags.

#include <cara/loader.h>
#include <cara/log.h>
#include <cara/mm.h>
#include <cara/types.h>

extern struct PageAllocator g_page_alloc;

// ---- ELF64 layout (System V ABI) ------------------------------------------

#define EI_MAG0      0
#define EI_MAG1      1
#define EI_MAG2      2
#define EI_MAG3      3
#define EI_CLASS     4
#define EI_DATA      5

#define ELFCLASS64   2
#define ELFDATA2LSB  1
#define ET_EXEC      2
#define EM_RISCV     243

#define PT_LOAD      1

#define PF_X         0x1
#define PF_W         0x2
#define PF_R         0x4

struct Elf64Header {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
};

struct Elf64Phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
};

// ---- Helpers ---------------------------------------------------------------

static u64 align_down(u64 v, u64 a)
{
    return v & ~(a - 1);
}

static u64 align_up(u64 v, u64 a)
{
    return (v + a - 1) & ~(a - 1);
}

static u64 prot_from_flags(u32 p_flags)
{
    u64 prot = PTE_V | PTE_U | PTE_A;
    if (p_flags & PF_R) {
        prot |= PTE_R;
    }
    if (p_flags & PF_W) {
        prot |= PTE_W | PTE_D;
    }
    if (p_flags & PF_X) {
        prot |= PTE_X;
    }
    return prot;
}

static int validate_header(const struct Elf64Header *h, usize size)
{
    if (size < sizeof(struct Elf64Header)) {
        return CARA_EINVAL;
    }
    if (h->e_ident[EI_MAG0] != 0x7F || h->e_ident[EI_MAG1] != 'E'
        || h->e_ident[EI_MAG2] != 'L' || h->e_ident[EI_MAG3] != 'F') {
        return CARA_EBADMAGIC;
    }
    if (h->e_ident[EI_CLASS] != ELFCLASS64) {
        return CARA_EBADVERSION;
    }
    if (h->e_ident[EI_DATA] != ELFDATA2LSB) {
        return CARA_EBADVERSION;
    }
    if (h->e_type != ET_EXEC) {
        return CARA_EINVAL;
    }
    if (h->e_machine != EM_RISCV) {
        return CARA_EINVAL;
    }
    if (h->e_phentsize < sizeof(struct Elf64Phdr)) {
        return CARA_EINVAL;
    }
    u64 phdr_end = h->e_phoff + (u64)h->e_phnum * h->e_phentsize;
    if (phdr_end < h->e_phoff || phdr_end > size) {
        return CARA_ERANGE;
    }
    return CARA_EOK;
}

// Lay one PT_LOAD segment down into the user PT.
[[nodiscard]] static int load_segment(const u8 *blob, usize blob_size,
                                      const struct Elf64Phdr *p,
                                      struct PageTable *pt)
{
    if (p->p_offset + p->p_filesz > blob_size
        || p->p_offset + p->p_filesz < p->p_offset) {
        return CARA_ERANGE;
    }
    if (p->p_memsz < p->p_filesz) {
        return CARA_EINVAL;
    }

    u64 va_lo = align_down(p->p_vaddr, CARA_PAGE_SIZE);
    u64 va_hi = align_up(p->p_vaddr + p->p_memsz, CARA_PAGE_SIZE);
    u32 n_pages = (u32)((va_hi - va_lo) / CARA_PAGE_SIZE);
    u64 prot = prot_from_flags(p->p_flags);

    LOG_DEBUG("elf ", "PT_LOAD vaddr=0x%llx filesz=%llu memsz=%llu flags=0x%x pages=%u",
              p->p_vaddr, p->p_filesz, p->p_memsz, p->p_flags, n_pages);

    u64 seg_first = p->p_vaddr;
    u64 seg_file_last = p->p_vaddr + p->p_filesz;       // exclusive

    for (u32 j = 0; j < n_pages; j++) {
        u64 phys = Page_Alloc(&g_page_alloc, 1);
        if (phys == 0) {
            return CARA_ENOMEM;
        }
        u8 *dst = (u8 *)Mm_PhysToVirt(phys);
        // Page is already zeroed by Page_Alloc.

        u64 page_va = va_lo + (u64)j * CARA_PAGE_SIZE;
        u64 page_end = page_va + CARA_PAGE_SIZE;

        // Compute the overlap of [seg_first, seg_file_last) with this page.
        u64 copy_lo = page_va > seg_first ? page_va : seg_first;
        u64 copy_hi = page_end < seg_file_last ? page_end : seg_file_last;

        if (copy_lo < copy_hi) {
            u64 in_seg = copy_lo - p->p_vaddr;
            u64 file_off = p->p_offset + in_seg;
            u64 dst_off = copy_lo - page_va;
            u64 len = copy_hi - copy_lo;
            for (u64 k = 0; k < len; k++) {
                dst[dst_off + k] = blob[file_off + k];
            }
        }
        // Bytes past p_filesz remain zero (covers .bss).

        int rc = Page_Map(pt, page_va, phys, prot);
        if (rc != CARA_EOK) {
            Page_Free(&g_page_alloc, phys, 1);
            return rc;
        }
    }
    return CARA_EOK;
}

[[nodiscard]] int Croi_LoadElf(const void *blob, usize size,
                               struct PageTable *pt, u64 *entry_va_out)
{
    if (!blob || !pt || !entry_va_out) {
        return CARA_EINVAL;
    }
    const u8 *bytes = (const u8 *)blob;
    const struct Elf64Header *h = (const struct Elf64Header *)bytes;

    int rc = validate_header(h, size);
    if (rc != CARA_EOK) {
        return rc;
    }
    LOG_DEBUG("elf ", "ET_EXEC RISCV entry=0x%llx phnum=%u", h->e_entry,
              h->e_phnum);

    for (u32 i = 0; i < h->e_phnum; i++) {
        const struct Elf64Phdr *p = (const struct Elf64Phdr *)(bytes + h->e_phoff
                                                               + (u64)i
                                                                   * h->e_phentsize);
        if (p->p_type != PT_LOAD) {
            continue;
        }
        rc = load_segment(bytes, size, p, pt);
        if (rc != CARA_EOK) {
            return rc;
        }
    }

    *entry_va_out = h->e_entry;
    return CARA_EOK;
}
