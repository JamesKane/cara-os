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
    LIB_VIRT (rwx) : ORIGIN = 0x0000000040000000,   LENGTH = 64K
}

PHDRS
{
    boot     PT_LOAD FLAGS(0x5);  /* R E -- low-phys boot */
    text     PT_LOAD FLAGS(0x5);  /* R E -- upper-half kernel */
    rodata   PT_LOAD FLAGS(0x4);  /* R   */
    data     PT_LOAD FLAGS(0x6);  /* RW  */
    lib_exec PT_LOAD FLAGS(0x5);  /* R E -- shared library region (user VA 0x4000_0000) */
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

    /* Embedded U-mode program. Croi_SpawnUserTask maps these bytes
     * into a user page table at CARA_USER_TEXT_BASE. The ALIGN(4K)
     * pair ensures the section occupies whole pages so per-page
     * mapping with PTE_USER_RX doesn't leak adjacent rodata. */
    .user_text : ALIGN(4K) {
        __user_text_start = .;
        KEEP(*(.user_text))
        . = ALIGN(4K);
        __user_text_end = .;
    } > RAM_VIRT AT > RAM_PHYS :rodata

    /* Embedded U-mode ELF. The build generates a small .S that
     * .incbin's the userhello.elf cross-build artefact into this
     * section; Croi_SpawnUserTaskFromElf parses these bytes via the
     * loader and maps PT_LOAD segments into a fresh user PT. */
    .user_elf : ALIGN(8) {
        __user_elf_start = .;
        KEEP(*(.user_elf))
        __user_elf_end = .;
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
        /* exec.library LMA window — same PC-relative-reach problem
         * as the kernel-image extents above, so we route the .exec_lib
         * LOADADDR / SIZEOF values through this rodata literal pool.
         * Used by Croi_ExecLib_InstallMapping (per-task PT install)
         * and Croi_ExecLib_KernelView (kernel-side direct-map view). */
        _exec_lib_image_lma_start = .;
        QUAD(LOADADDR(.exec_lib))
        _exec_lib_image_lma_size = .;
        QUAD(SIZEOF(.exec_lib))
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

    /* exec.library shared library region. VMA at user-half
     * 0x0000_0000_4000_0000 (the SASOS library shared region per
     * ARCHITECTURE.md §4.3); LMA flows in RAM_PHYS so the bytes are
     * part of the kernel ELF QEMU loads. The kernel writes the
     * region (Croi_MakeLibrary populating fields, vec-table copy)
     * via the upper-half direct-map view at
     *   __exec_lib_lma_start + KERNEL_VA_OFFSET
     * (i.e. Mm_PhysToVirt(__exec_lib_lma_start) in C). Per-task
     * page tables map the physical pages back to VMA 0x4000_0000
     * with R+X+U so user code can read+execute. See docs/LVO.md
     * §3 for the runtime layout (vec[i] at base[-1-i]).
     *
     * Placed after the NOLOAD sections so the LMA cursor in
     * RAM_PHYS flows past .bss/.stack first; otherwise lld assigns
     * .bss the cursor it had after .got, leaving an LMA collision. */
    .exec_lib : ALIGN(4K) {
        __exec_lib_neg_start = .;
        /* Negative-side reserve: 2 KiB = 256 vec slots maximum.
         * Croi_MakeLibrary populates the actual slot count at boot;
         * lib_NegSize records the populated span. */
        . = . + 0x800;
        __exec_lib_base = .;
        /* Positive-side reserve: struct Library + private state.
         * One page is generous for a struct Library (~64 bytes) plus
         * an ExecBase-style private region. */
        . = . + 4K;
        __exec_lib_pos_end = .;
        /* Trampolines: per-LVO ecall stubs from
         * src/croi/exec_lib/trampolines.S. KEEP() prevents the
         * linker dropping them when no other section references the
         * Cara_Trampoline_* symbols (the references arrive when the
         * generated exec_vec.c lands in C9). */
        KEEP(*(.lib_text.exec))
        /* intuition.library's per-LVO syscall trampolines share this
         * one RX+U region (src/croi/intuition_lib/trampolines.S).
         * intuition's base + vec table live in the SASOS shared heap,
         * but its vec entries point at these trampolines. */
        KEEP(*(.lib_text.intuition))
        /* utility.library's LVOs are `local` flavour — their impl
         * bodies (src/croi/utility_lib/tag_ops.c) live here in the
         * shared RX+U region and are JALR'd directly from U-mode, with
         * no trampoline. utility's base + vec table live in the SASOS
         * shared heap (entry.c); the vec entries point in here. */
        KEEP(*(.lib_text.utility))
        /* dos.library's `syscall`-flavour trampolines (IoErr in L3.1;
         * the packet ops are `server` flavour). Same shared RX+U region;
         * dos's base + vec live in the SASOS shared heap. */
        KEEP(*(.lib_text.dos))
        . = ALIGN(4K);
        __exec_lib_end = .;
    } > LIB_VIRT AT > RAM_PHYS :lib_exec

    /* LMA values are exposed to C code via the .kernel_extents
     * literal pool above (_exec_lib_image_lma_start /
     * _exec_lib_image_lma_size); the absolute LMA addresses can't
     * be referenced PC-relative from upper-half kernel code. */

    /* Page-aligned end-of-image marker. Mm_Init subtracts the range
     * [_kernel_phys_start, _kernel_phys_end) from the FDT-derived
     * memory map so the page allocator never hands out kernel memory.
     * Placed last so its LOADADDR includes everything before it
     * including .exec_lib. */
    .kernel_end (NOLOAD) : ALIGN(4K) {
    } > RAM_VIRT AT > RAM_PHYS :data
    _kernel_phys_start = KERNEL_LOAD_ADDR;
    /* .exec_lib lives in a separate VMA region (LIB_VIRT) but
     * shares RAM_PHYS for its load address, so the kernel image's
     * physical extent reaches past it. lld doesn't always advance
     * .kernel_end's LMA cursor through .exec_lib (the cross-region
     * insert confuses the AT > RAM_PHYS flow), so we compute the
     * end explicitly from .exec_lib's LMA + size. */
    _kernel_phys_end   = LOADADDR(.exec_lib) + SIZEOF(.exec_lib);

    /DISCARD/ : {
        *(.note .note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.riscv.attributes)
    }
}
