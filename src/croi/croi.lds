/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Croi linker script for the QEMU virt RV64 kernel ELF.
 *
 * Layout pattern follows VectraOS's riscv64-virt.ld (BSD-licensed kernel
 * project at /Users/jkane/Development/VectraOS) — adapted to CaraOS naming
 * and the SASOS-on-Sv39 layout from docs/ARCHITECTURE.md §3.
 *
 * QEMU virt RAM starts at 0x80000000. With `-bios default -kernel ...`
 * QEMU places OpenSBI at 0x80000000 and the kernel ELF at 0x80200000.
 * OpenSBI runs M-mode and hands off in S-mode with:
 *   a0 = hartid
 *   a1 = phys(DTB)
 *   MMU off, sstatus.SIE = 0
 *
 * Dual-map kernel:
 *   - BOOT region (16 KiB at VMA = LMA = 0x80200000) holds .text._start
 *     and the boot Sv39 root page table. Runs pre-MMU; PC-relative only;
 *     no stack.
 *   - RAM_VIRT region (kernel image VMA at 0xFFFFFFC080204000) holds
 *     .text / .rodata / .data / .bss. LMA continues immediately after
 *     BOOT (RAM_PHYS at 0x80204000) so the kernel ELF stays one
 *     PT_LOAD-friendly slab QEMU can copy into RAM in one go.
 *   - KERNEL_VA_OFFSET = 0xFFFFFFC0_00000000 (Sv39 sign-extended upper
 *     half — the direct map region per ARCHITECTURE.md §3.4).
 *
 * Splanc.efi (UEFI bootloader for real OrangePi RV2 hardware) is a
 * separate concern. For QEMU the boot stub here does the page-table
 * setup itself, so we don't need Splanc to reach the kernel-virt VAs.
 */

OUTPUT_ARCH(riscv)
OUTPUT_FORMAT("elf64-littleriscv")
ENTRY(_start)

KERNEL_LOAD_ADDR  = 0x80200000;
KERNEL_VA_OFFSET  = 0xFFFFFFC000000000;

MEMORY
{
    BOOT     (rwx) : ORIGIN = 0x80200000,           LENGTH = 16K
    RAM_PHYS (rwx) : ORIGIN = 0x80204000,           LENGTH = 128M - 16K
    RAM_VIRT (rwx) : ORIGIN = 0xFFFFFFC080204000,   LENGTH = 128M - 16K
}

PHDRS
{
    boot   PT_LOAD FLAGS(0x5);    /* R E -- low-phys boot */
    text   PT_LOAD FLAGS(0x5);    /* R E -- upper-half kernel */
    rodata PT_LOAD FLAGS(0x4);    /* R   */
    data   PT_LOAD FLAGS(0x6);    /* RW  */
}

SECTIONS
{
    /* --- Low-phys boot (VMA = LMA = 0x80200000) ----------------------- */

    .boot.text : ALIGN(4K) {
        KEEP(*(.text._start))
    } > BOOT :boot

    .boot.bss (NOLOAD) : ALIGN(4K) {
        _boot_root_pt = .;
        . += 4K;
    } > BOOT :boot

    /* --- Upper-half kernel (VMA upper-half, LMA RAM_PHYS) ------------- */

    .text : ALIGN(4K) {
        *(.text .text.*)
    } > RAM_VIRT AT > RAM_PHYS :text

    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > RAM_VIRT AT > RAM_PHYS :rodata

    /* Literal pool of kernel-image extents. The pre-MMU low-phys
     * symbols can't be referenced from upper-half code via PC-relative
     * (39-bit gap exceeds auipc+addi); we materialise their values into
     * upper-half rodata at link time and C code reads from the
     * upper-half address. Pattern from VectraOS riscv64-virt.ld
     * (.kernel_image_descriptor). */
    .kernel_extents : ALIGN(8) {
        _kernel_image_phys_start = .;
        QUAD(_kernel_phys_start)
        _kernel_image_phys_end = .;
        QUAD(_kernel_phys_end)
    } > RAM_VIRT AT > RAM_PHYS :rodata

    /* In-kernel self-test registry. Each KERNEL_TEST() macro emits a
     * struct KernelTestEntry into this section; the runner walks
     * [__kernel_tests_start, __kernel_tests_end) at boot. */
    .kernel_tests : ALIGN(8) {
        __kernel_tests_start = .;
        KEEP(*(.kernel_tests))
        __kernel_tests_end = .;
    } > RAM_VIRT AT > RAM_PHYS :rodata

    .data : ALIGN(4K) {
        *(.data .data.*)
        *(.sdata .sdata.*)
    } > RAM_VIRT AT > RAM_PHYS :data

    .got : ALIGN(8) {
        *(.got)
        *(.got.plt)
    } > RAM_VIRT AT > RAM_PHYS :data

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > RAM_VIRT AT > RAM_PHYS :data

    .stack (NOLOAD) : ALIGN(16) {
        __boot_stack_bottom = .;
        . += 16K;
        __boot_stack_top = .;
    } > RAM_VIRT AT > RAM_PHYS :data

    /* Page-aligned end-of-image marker. Mm_Init subtracts the range
     * [_kernel_phys_start, _kernel_phys_end) from the FDT-derived
     * memory map so the page allocator never hands out kernel memory. */
    .kernel_end (NOLOAD) : ALIGN(4K) {
    } > RAM_VIRT AT > RAM_PHYS :data
    _kernel_phys_start = KERNEL_LOAD_ADDR;
    _kernel_phys_end   = LOADADDR(.kernel_end);

    /DISCARD/ : {
        *(.note .note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.riscv.attributes)
    }
}
