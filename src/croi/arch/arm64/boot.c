// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 upper-half boot entry (epic H.7.2b, docs/ARM64.md §5).
//
// H.7.2b reaches a portable mm bring-up on AArch64: _start.S enables stage-1
// paging and branches here (upper half). This parses the real device tree and
// initialises the physical memory manager — the page allocator + heap — through
// the SAME shared cara_fdt / cara_mm code the RISC-V kernel uses, proving the
// portable memory subsystem works on a second ISA. It is still a bring-up stub
// (it does not yet construct the libraries / scheduler / userland that the full
// croi_entry does); when those are ported it is replaced by croi_entry proper.
//
// NB the page-TABLE walk (Page_Map / Croi_NewKernelPT) is NOT used here and is
// not yet correct on AArch64 — block-vs-page is encoded by level, which the
// generic walk doesn't yet know (see cara/arch/arm64/arch_pte.h). The page
// *allocator* + physmap never touch that encoding, so this path is sound. The
// walk reconciliation + arch_mmu_* land with the scheduler (H.7.4).

#include <cara/alloc.h>
#include <cara/arch.h>
#include <cara/fdt.h>
#include <cara/mm.h>
#include <cara/types.h>

CARA_NORETURN void arm64_kernel_main(u64 dtb_phys);

// QEMU `-M virt` (aarch64) RAM base + kernel load address (see kernel.lds).
// These match what _start.S already assumes (the boot block descriptors map
// PA 0x40000000); the FDT we parse below is then authoritative for the memory
// map. QEMU drops the generated DTB at the base of RAM and does NOT pass it in
// x0 for an ELF `-kernel`, so we discover it there when the boot register is 0.
#define ARM64_RAM_BASE 0x40000000ull
#define ARM64_KERNEL_PHYS_BASE 0x40200000ull

// End-of-image upper-half VA from kernel.lds; phys = VA - KERNEL_VA_OFFSET.
extern char __kernel_end_virt[];

// pt.c references these by `extern` (Page_Alloc/Free over the global allocator);
// the heap is the active allocator backing Croi_Alloc. Zeroed BSS at boot.
struct PageAllocator g_page_alloc;
struct Heap g_heap;

// Minimal "0x"-prefixed 64-bit hex (the early console only does strings and no
// printf backend exists yet — mirrors leaning on arch_console_*).
static void put_hex64(u64 v)
{
    static const char digits[] = "0123456789abcdef";
    char buf[2 + 16 + 1];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = digits[(v >> ((15 - i) * 4)) & 0xf];
    }
    buf[18] = '\0';
    arch_console_puts(buf);
}

static void put_line(const char *label, u64 v)
{
    arch_console_puts(label);
    put_hex64(v);
    arch_console_puts("\n");
}

// Scan low RAM below the kernel image for the FDT magic (devicetree spec: the
// header's first big-endian u32 is 0xd00dfeed, i.e. 0xedfe0dd0 read little-
// endian). QEMU places the DTB at the base of RAM. Returns the phys address, or
// 0 if not found. Bounded to [RAM_BASE, kernel) so it can't hit the kernel image.
static u64 arm64_find_dtb(void)
{
    for (u64 p = ARM64_RAM_BASE; p < ARM64_KERNEL_PHYS_BASE; p += 8) {
        const volatile u32 *w = (const volatile u32 *)Mm_PhysToVirt(p);
        if (*w == 0xedfe0dd0u) {
            return p;
        }
    }
    return 0;
}

CARA_NORETURN void arm64_kernel_main(u64 dtb_phys)
{
    arch_console_puts("\nCaraOS croi \xe2\x80\x94 arch=arm64 (H.7.2b mm bring-up)\n");
    put_line("arm64 boot: arm64_kernel_main @ ", (u64)(uptr)&arm64_kernel_main);

    // ---- Acquire the DTB. Prefer the boot register; fall back to a scan. ----
    struct Fdt fdt;
    u64 dtb = dtb_phys;
    if (dtb == 0 || Fdt_Open(&fdt, Mm_PhysToVirt(dtb)) != CARA_EOK) {
        dtb = arm64_find_dtb();
        if (dtb == 0 || Fdt_Open(&fdt, Mm_PhysToVirt(dtb)) != CARA_EOK) {
            arch_console_puts("arm64 boot: FATAL: no usable device tree found\n");
            arch_halt();
        }
    }
    put_line("arm64 boot: DTB @ ", dtb);
    put_line("arm64 boot: DTB totalsize = ", fdt.totalsize);

    // ---- Physical memory map (carve out kernel image + DTB). ----
    u64 kphys_start = ARM64_KERNEL_PHYS_BASE;
    u64 kphys_end = Mm_VirtToPhys(__kernel_end_virt);
    u64 dtb_start = dtb;
    u64 dtb_end = dtb + fdt.totalsize;

    put_line("arm64 boot: kernel phys end = ", kphys_end);

    struct PhysMap pm;
    int rc = Mm_PhysMapFromFdt(&pm, &fdt, kphys_start, kphys_end, dtb_start, dtb_end);
    if (rc != CARA_EOK) {
        put_line("arm64 boot: FATAL: Mm_PhysMapFromFdt rc = ", (u64)(i64)rc);
        arch_halt();
    }
    put_line("arm64 boot: RAM total bytes  = ", pm.total_bytes);
    put_line("arm64 boot: usable bytes     = ", pm.usable_bytes);

    // ---- Page allocator. ----
    rc = Page_Init(&g_page_alloc, &pm);
    if (rc != CARA_EOK) {
        put_line("arm64 boot: FATAL: Page_Init rc = ", (u64)(i64)rc);
        arch_halt();
    }
    put_line("arm64 boot: free pages       = ", g_page_alloc.free_pages);

    // Round-trip a single page through the allocator to prove it works.
    u64 pg = Page_Alloc(&g_page_alloc, 1);
    if (pg == 0) {
        arch_console_puts("arm64 boot: FATAL: Page_Alloc returned 0\n");
        arch_halt();
    }
    put_line("arm64 boot: Page_Alloc(1)    = ", pg);
    Page_Free(&g_page_alloc, pg, 1);

    // ---- Kernel heap on top of the allocator. ----
    rc = Heap_Init(&g_heap, &g_page_alloc);
    if (rc != CARA_EOK) {
        put_line("arm64 boot: FATAL: Heap_Init rc = ", (u64)(i64)rc);
        arch_halt();
    }
    Heap_SetActive(&g_heap);
    void *blk = Croi_Alloc(128);
    if (!blk) {
        arch_console_puts("arm64 boot: FATAL: Croi_Alloc(128) failed\n");
        arch_halt();
    }
    put_line("arm64 boot: Croi_Alloc(128)  = ", (u64)(uptr)blk);

    arch_console_puts("CaraOS arm64 boot: ok (paged + mm up)\n");
    arch_halt();
}
