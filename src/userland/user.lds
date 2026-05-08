/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Linker script for the embedded U-mode hello program. Lays out
 *   .text + .rodata at user VA 0x10000 (one PT_LOAD, RX),
 *   .data at the next page boundary (PT_LOAD, RW),
 *   .bss at the page boundary after that (NOLOAD, RW).
 *
 * The kernel's ELF loader walks PT_LOAD entries, allocates pages,
 * copies file bytes for the [p_offset, p_offset + p_filesz) slice
 * and zero-fills the [p_filesz, p_memsz) tail (so .bss works without
 * explicit storage in the file).
 */

OUTPUT_ARCH(riscv)
OUTPUT_FORMAT("elf64-littleriscv")
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(0x5);   /* R   E */
    data PT_LOAD FLAGS(0x6);   /* R W */
}

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(4K) {
        KEEP(*(.text._start))
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } :text

    .data : ALIGN(4K) {
        *(.data .data.*)
        *(.sdata .sdata.*)
    } :data

    .bss (NOLOAD) : ALIGN(16) {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
        *(.riscv.attributes)
    }
}
