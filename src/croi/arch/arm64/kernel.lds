/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Croi linker script for the QEMU virt AArch64 kernel ELF (epic H.7.2,
 * docs/ARM64.md).
 *
 * QEMU `-M virt` (aarch64) RAM starts at 0x40000000. With `-kernel <elf>`
 * QEMU loads the ELF by its program headers (physical addresses) and enters
 * at e_entry (_start) with the MMU off, at EL1 (or EL2 — _start drops to EL1),
 * x0 = phys(DTB).
 *
 * Dual-map kernel (the AArch64 analogue of arch/riscv64/croi.lds, which uses
 * Sv39; here the equivalent is the AArch64 4 KiB-granule / 39-bit-VA stage-1
 * with T0SZ=T1SZ=25):
 *   - BOOT region (64 KiB at VMA = LMA = 0x40200000) holds .text._start and
 *     the two boot stage-1 root tables (TTBR0 identity-low + TTBR1 upper-half).
 *     Runs pre-MMU; PC-relative only; no stack.
 *   - RAM_PHYS (LMA) continues at 0x40210000; the upper-half kernel image
 *     (.text / .rodata / .data / .bss / .stack) has VMA at
 *     RAM_VIRT = KERNEL_VA_OFFSET + 0x40210000 so the kernel runs at SASOS
 *     upper-half VAs once the MMU is on.
 *   - KERNEL_VA_OFFSET = 0xFFFFFFC0_00000000 — the same upper-half base as the
 *     RISC-V kernel (bit 38 set in a 39-bit VA), which is canonical for the
 *     AArch64 TTBR1 region (bits[63:39] all 1) when T1SZ = 25.
 *
 * The boot tables map RAM with a single 1 GiB block descriptor each (PA
 * 0x40000000 covers both BOOT and the kernel image), exactly as the RISC-V
 * boot PT uses 1 GiB Sv39 blocks. The MMU/upper-half rework toward the full
 * cara_mm direct map (and a CARA_ARCH-selected croi.lds) lands with the mm
 * integration slice.
 */

OUTPUT_ARCH(aarch64)
OUTPUT_FORMAT("elf64-littleaarch64")
ENTRY(_start)

KERNEL_LOAD_ADDR = 0x40200000;
KERNEL_VA_OFFSET = 0xFFFFFFC000000000;

MEMORY
{
    BOOT     (rwx) : ORIGIN = 0x40200000,           LENGTH = 64K
    RAM_PHYS (rwx) : ORIGIN = 0x40210000,           LENGTH = 128M - 64K
    RAM_VIRT (rwx) : ORIGIN = 0xFFFFFFC040210000,   LENGTH = 128M - 64K
}

PHDRS
{
    boot   PT_LOAD FLAGS(0x5);  /* R E -- low-phys boot */
    text   PT_LOAD FLAGS(0x5);  /* R E -- upper-half kernel */
    rodata PT_LOAD FLAGS(0x4);  /* R   */
    data   PT_LOAD FLAGS(0x6);  /* RW  */
}

SECTIONS
{
    /* --- Low-phys boot (VMA = LMA = 0x40200000) ----------------------- */

    .boot.text : ALIGN(4K) {
        KEEP(*(.text._start))
    } > BOOT :boot

    /* The two boot stage-1 root tables (4 KiB each). NOLOAD, so _start.S
     * zeroes them before installing the block descriptors. */
    .boot.bss (NOLOAD) : ALIGN(4K) {
        _boot_ttbr0 = .;
        . += 4K;
        _boot_ttbr1 = .;
        . += 4K;
    } > BOOT :boot

    /* --- Upper-half kernel (VMA upper-half, LMA RAM_PHYS) ------------- */

    .text : ALIGN(4K) {
        *(.text .text.*)
    } > RAM_VIRT AT > RAM_PHYS :text

    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
    } > RAM_VIRT AT > RAM_PHYS :rodata

    .data : ALIGN(4K) {
        *(.data .data.*)
    } > RAM_VIRT AT > RAM_PHYS :data

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > RAM_VIRT AT > RAM_PHYS :data

    .stack (NOLOAD) : ALIGN(16) {
        __boot_stack_bottom = .;
        . += 16K;
        __boot_stack_top = .;
    } > RAM_VIRT AT > RAM_PHYS :data

    /DISCARD/ : {
        *(.note .note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
