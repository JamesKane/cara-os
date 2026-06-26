// SPDX-License-Identifier: BSD-2-Clause
//
// Croi AArch64 boot (epic H.7.1, docs/ARM64.md §3).
//
// Entered from QEMU `-M virt` (aarch64) with `-kernel <elf>`: QEMU loads the
// ELF by its program headers and jumps to _start at VMA = LMA = 0x40200000
// with the MMU off and x0 = phys(DTB). The starting exception level is EL1 by
// default; if QEMU is configured with virtualization the entry is EL2, so we
// drop to EL1 first. (No EL3: QEMU virt has secure=off by default.)
//
// H.7.1 is boot-to-print, so this is the whole boot: bring the CPU to a known
// EL1 state with the MMU off, set a stack, zero BSS, and tail into the C boot
// stub. Stage-1 paging + the jump to the upper-half croi_entry land in H.7.2.

.section .text._start, "ax"
.globl _start
.type  _start, @function
_start:
    // QEMU passes the DTB phys address in x0; stash it across the EL drop.
    mov     x19, x0

    // Mask all interrupts (D,A,I,F) for the duration of bring-up.
    msr     DAIFSet, #0xf

    // What EL did we enter at? CurrentEL[3:2] = current exception level.
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    and     x0, x0, #3
    cmp     x0, #2
    b.ne    .Lat_el1                // already EL1 (QEMU default) → continue

    // ---- Drop EL2 -> EL1 -------------------------------------------------
    // EL1 runs in AArch64 (HCR_EL2.RW = 1).
    mov     x0, #(1 << 31)
    msr     HCR_EL2, x0
    // Don't trap EL1/EL0 FP/SIMD at EL2 (v8.0 no-trap baseline).
    mov     x0, #0x33ff
    msr     CPTR_EL2, x0
    // Clean SCTLR_EL1: MMU off, caches off, little-endian, RES1 bits set.
    ldr     x0, =0x30d00800
    msr     SCTLR_EL1, x0
    // Return into EL1h (own stack pointer), DAIF masked, at .Lat_el1.
    mov     x0, #0x3c5              // M=EL1h, D=A=I=F=1
    msr     SPSR_EL2, x0
    adr     x0, .Lat_el1
    msr     ELR_EL2, x0
    eret

.Lat_el1:
    // ---- At EL1, MMU off -------------------------------------------------
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

    // arm64_boot_main(dtb_phys). [[noreturn]]; if it returns, park.
    mov     x0, x19
    bl      arm64_boot_main

.Lpark:
    wfi
    b       .Lpark
.size   _start, . - _start
