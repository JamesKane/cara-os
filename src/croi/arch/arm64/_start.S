// SPDX-License-Identifier: BSD-2-Clause
//
// Croi AArch64 boot (epic H.7.2, docs/ARM64.md §3).
//
// Entered from QEMU `-M virt` (aarch64) with `-kernel <elf>`: QEMU loads the
// ELF by its program headers and jumps to _start (in the low-phys BOOT region
// at VMA = LMA = 0x40200000) with the MMU off, at EL1 by default (EL2 if QEMU
// is configured with virtualization — we drop to EL1), x0 = phys(DTB).
//
// Pre-MMU sequence (.text._start, low-phys, PC-relative only, no stack):
//   1. Bring the CPU to EL1, interrupts masked.
//   2. Build two boot stage-1 root tables (4 KiB each), each a single level
//      with 1 GiB block descriptors (the AArch64 analogue of the RISC-V boot
//      PT's 1 GiB Sv39 blocks):
//        _boot_ttbr0[0]   = device block @ PA 0x00000000 (identity; PL011)
//        _boot_ttbr0[1]   = normal block @ PA 0x40000000 (identity; RAM, X)
//        _boot_ttbr1[256] = device block @ PA 0x00000000 (upper-half)
//        _boot_ttbr1[257] = normal block @ PA 0x40000000 (upper-half RAM, X)
//      TTBR0 (identity low) keeps the pre-MMU PC + the low PL011 console
//      resolvable across the SCTLR.M write; TTBR1 holds the eventual upper-half
//      kernel VAs. Index 257 = the L1 index of KERNEL_VA_OFFSET+0x40000000
//      under T1SZ=25 (39-bit VA, bit 38 set), the Sv39 equivalent.
//   3. Program MAIR / TCR (T0SZ=T1SZ=25, 4 KiB granule) / TTBR0 / TTBR1,
//      barrier + TLBI, then enable the MMU (SCTLR_EL1.M|C|I).
//   4. Load the absolute upper-half VA of _high_entry from a literal pool and
//      branch to it.
//
// Post-MMU (.text upper-half, _high_entry): set SP, zero BSS, call the C entry.

// AArch64 stage-1 block-descriptor values (1 GiB L1 block). See docs/ARM64.md.
//   Normal RAM, kernel RWX: OA | UXN(54) | AF(10) | SH=inner(9:8) |
//                           AttrIdx=1(4:2) | block(1:0=01)
//     low attrs = 0x705 ; UXN = 1<<54 ; PA 0x40000000
//   Device-nGnRnE:         OA | UXN(54) | PXN(53) | AF(10) |
//                           AttrIdx=0(4:2) | block(1:0=01)
//     low attrs = 0x401 ; UXN|PXN = (1<<54)|(1<<53) ; PA 0x00000000
.equ DESC_RAM_40000000, 0x0040000040000705
.equ DESC_DEV_00000000, 0x0060000000000401

// MAIR_EL1: Attr0 = 0x00 (device-nGnRnE), Attr1 = 0xFF (normal write-back).
.equ MAIR_VAL, 0xFF00
// TCR_EL1: T0SZ=T1SZ=25, IRGN/ORGN=WB, SH=inner, TG0=4K, TG1=4K, IPS=40-bit.
.equ TCR_VAL, 0x2B5193519
// SCTLR_EL1: ARMv8 RES1 baseline | M(0) | C(2) | I(12) = enable MMU + caches.
.equ SCTLR_ON, 0x30d01805
// SCTLR_EL1: same RES1 baseline, MMU off (used during the EL2->EL1 drop).
.equ SCTLR_OFF, 0x30d00800

.section .text._start, "ax"
.globl _start
.type  _start, @function
_start:
    // QEMU passes the DTB phys address in x0; stash it across the EL drop.
    mov     x19, x0

    // Mask all interrupts (D,A,I,F) for the duration of bring-up.
    msr     DAIFSet, #0xf

    // Drop to EL1 if we entered at EL2 (CurrentEL[3:2]).
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    and     x0, x0, #3
    cmp     x0, #2
    b.ne    .Lat_el1

    mov     x0, #(1 << 31)          // HCR_EL2.RW = 1: EL1 is AArch64
    msr     HCR_EL2, x0
    mov     x0, #0x33ff             // CPTR_EL2: don't trap EL1/0 FP/SIMD
    msr     CPTR_EL2, x0
    ldr     x0, =SCTLR_OFF          // SCTLR_EL1: MMU off, RES1 baseline
    msr     SCTLR_EL1, x0
    mov     x0, #0x3c5              // SPSR_EL2: M=EL1h, D=A=I=F=1
    msr     SPSR_EL2, x0
    adr     x0, .Lat_el1
    msr     ELR_EL2, x0
    eret

.Lat_el1:
    // Enable FP/SIMD at EL1 and EL0 (CPACR_EL1.FPEN = 0b11, no trap) so the
    // NEON register file save/restore in arch_ctx_switch and U-mode FP apps
    // don't take a trapped-access exception. (The kernel itself is
    // integer-only, -mgeneral-regs-only, but ctx_switch.S touches v0..v31.)
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    // ---- Build the two boot stage-1 root tables (MMU still off) -------
    // Zero both 4 KiB tables (NOLOAD → garbage on entry).
    adr     x0, _boot_ttbr0
    add     x1, x0, #(2 * 4096)     // end of _boot_ttbr1
.Lzero_pt:
    str     xzr, [x0], #8
    cmp     x0, x1
    b.lo    .Lzero_pt

    ldr     x2, =DESC_RAM_40000000  // normal RAM, kernel RWX, PA 0x40000000
    ldr     x3, =DESC_DEV_00000000  // device, PA 0x00000000

    // TTBR0 (identity low half).
    adr     x0, _boot_ttbr0
    str     x3, [x0, #(0 * 8)]      // [0]   device  @ PA 0
    str     x2, [x0, #(1 * 8)]      // [1]   RAM     @ PA 0x40000000

    // TTBR1 (upper half). VA 0xFFFFFFC0_00000000 → L1 index 256 (device),
    // VA 0xFFFFFFC0_40000000 → L1 index 257 (RAM), under T1SZ=25.
    adr     x1, _boot_ttbr1
    str     x3, [x1, #(256 * 8)]    // [256] device  @ PA 0
    str     x2, [x1, #(257 * 8)]    // [257] RAM     @ PA 0x40000000

    // ---- Program the MMU registers -----------------------------------
    ldr     x4, =MAIR_VAL
    msr     MAIR_EL1, x4
    ldr     x4, =TCR_VAL
    msr     TCR_EL1, x4
    adr     x0, _boot_ttbr0
    msr     TTBR0_EL1, x0
    adr     x1, _boot_ttbr1
    msr     TTBR1_EL1, x1
    dsb     ish
    tlbi    vmalle1
    dsb     ish
    isb

    // ---- Enable the MMU + caches -------------------------------------
    ldr     x4, =SCTLR_ON
    msr     SCTLR_EL1, x4
    isb

    // ---- Jump to the upper-half entry --------------------------------
    // _high_entry's VMA is in the upper half (39-bit gap exceeds adrp reach),
    // so materialise its absolute VA from a literal and branch.
    ldr     x0, =_high_entry
    br      x0
.size   _start, . - _start


// === Upper-half post-MMU entry =====================================

.section .text, "ax"
.balign 4
.globl _high_entry
.type  _high_entry, @function
_high_entry:
    // PC is now in the upper half (resolved via TTBR1).
    ldr     x0, =__boot_stack_top
    mov     sp, x0

    // Zero BSS [__bss_start, __bss_end).
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
.Lbss_zero:
    cmp     x0, x1
    b.hs    .Lbss_done
    str     xzr, [x0], #8
    b       .Lbss_zero
.Lbss_done:

    // arm64_kernel_main(dtb_phys). [[noreturn]]; if it returns, park.
    mov     x0, x19
    bl      arm64_kernel_main

.Lpark:
    wfi
    b       .Lpark
.size   _high_entry, . - _high_entry
