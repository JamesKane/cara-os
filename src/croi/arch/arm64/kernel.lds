/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Croi linker script for the QEMU virt AArch64 kernel ELF (epic H.7.1,
 * docs/ARM64.md).
 *
 * QEMU `-M virt` (aarch64) RAM starts at 0x40000000. With `-kernel <elf>`
 * QEMU loads the ELF by its program headers (physical addresses) and enters
 * at e_entry (_start) with the MMU off, at EL1 (or EL2 — _start drops to
 * EL1). x0 holds the DTB physical address.
 *
 * H.7.1 is boot-to-print: the kernel runs entirely physical (MMU off), so this
 * is a flat low-half layout — VMA = LMA, one PT_LOAD-friendly slab at
 * 0x40200000 (RAM base + 2 MiB, mirroring the RV64 image's +2 MiB offset).
 * The SASOS upper-half split (KERNEL_VA_OFFSET, TTBR1) arrives with paging in
 * H.7.2, at which point this grows toward arch/riscv64's dual-map croi.lds.
 */

OUTPUT_ARCH(aarch64)
OUTPUT_FORMAT("elf64-littleaarch64")
ENTRY(_start)

KERNEL_LOAD_ADDR = 0x40200000;

SECTIONS
{
    . = KERNEL_LOAD_ADDR;

    .text : ALIGN(4K) {
        KEEP(*(.text._start))
        *(.text .text.*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4K) {
        *(.data .data.*)
    }

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    .stack (NOLOAD) : ALIGN(16) {
        __boot_stack_bottom = .;
        . += 16K;
        __boot_stack_top = .;
    }

    /DISCARD/ : {
        *(.note .note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
