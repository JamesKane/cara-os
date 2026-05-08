// SPDX-License-Identifier: BSD-2-Clause
//
// Croi RV64 boot. Pattern follows VectraOS's start.S — adapted to
// CaraOS naming. See docs/ARCHITECTURE.md §8 (boot handoff) and
// docs/HARDWARE_RV2.md §3 (Sv39 + 1 GiB blocks).
//
// Entered from OpenSBI in S-mode at VMA=LMA=0x80200000 with:
//   a0 = hartid
//   a1 = phys(DTB)
//   MMU off, sstatus.SIE = 0
//
// Pre-MMU sequence (.text._start at low-phys, PC-relative only, no
// stack):
//   1. Clear sstatus.SIE.
//   2. Build boot Sv39 root PT at _boot_root_pt (4 KiB, in BOOT
//      region):
//        L2[0]   = 1 GiB device block at PA 0x00000000 (identity)
//        L2[2]   = 1 GiB RAM block at PA 0x80000000 (identity, RWX)
//        L2[256] = 1 GiB device block at PA 0x00000000 (upper-half)
//        L2[258] = 1 GiB RAM block at PA 0x80000000 (upper-half, RWX)
//      Identity entries are needed so the instruction immediately after
//      `csrw satp` still resolves; upper-half entries are the eventual
//      kernel VAs.
//   3. Write satp = (Sv39 << 60) | (root_pt >> 12); sfence.vma.
//   4. PC-relative load of the upper-half _high_entry VA via a literal
//      pool (the 39-bit gap exceeds auipc+addi reach), then jr to it.
//
// Post-MMU (.text upper-half, _high_entry):
//   1. Set sp to upper-half __boot_stack_top.
//   2. Zero BSS.
//   3. Tail-call croi_entry(hartid, dtb_phys).

// PTE attribute bits (Volume II: RISC-V Privileged Architecture).
.equ PTE_V, 0x01
.equ PTE_R, 0x02
.equ PTE_W, 0x04
.equ PTE_X, 0x08
.equ PTE_G, 0x20
.equ PTE_A, 0x40
.equ PTE_D, 0x80

.equ PTE_DEV, (PTE_V | PTE_R | PTE_W |         PTE_G | PTE_A | PTE_D)
.equ PTE_RWX, (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)

.section .text._start, "ax"
.globl _start
.type  _start, @function
_start:
    // Park secondaries at SBI level for now — single hart for slice 1.
    bnez    a0, _park_hart

    // Stash hartid in tp; preserve a0/a1 for the C entry.
    mv      tp, a0

    // Clear sstatus.SIE; set SUM (bit 18) so S-mode can read U-mode
    // pages — needed for syscall buffers without per-byte get_user().
    csrci   sstatus, 0x2
    li      t0, (1 << 18)
    csrs    sstatus, t0

    // sscratch convention: 0 in kernel-mode, kstack_top in user-mode.
    // trap_entry.S relies on this for origin detection via csrrw swap.
    csrw    sscratch, zero

    // ---- Build boot Sv39 root PT at _boot_root_pt ----
    la      t0, _boot_root_pt

    // Zero the 4 KiB.
    mv      t1, t0
    li      t2, 4096
    add     t2, t1, t2
.Lzero_pt:
    sd      zero, 0(t1)
    addi    t1, t1, 8
    blt     t1, t2, .Lzero_pt

    // L2[0]: 1 GiB device block at PA 0. PPN = 0.
    li      t1, PTE_DEV
    sd      t1, 0(t0)               // index 0 * 8 = 0

    // L2[2]: 1 GiB RWX block at PA 0x80000000. PPN = 0x80000.
    li      t1, 0x80000
    slli    t1, t1, 10              // PPN << 10
    li      t2, PTE_RWX
    or      t1, t1, t2
    sd      t1, 16(t0)              // index 2 * 8 = 16

    // Upper-half L2 entries live at offsets 2048 / 2064; each exceeds
    // the 12-bit signed addi immediate, so we step the base in two
    // adds.
    addi    t3, t0, 2047
    addi    t3, t3, 1               // t3 = root_pt + 2048

    // L2[256]: 1 GiB device block at upper-half mapping of PA 0.
    li      t1, PTE_DEV
    sd      t1, 0(t3)               // index 256 * 8 = 2048

    // L2[258]: 1 GiB RWX block at upper-half mapping of PA 0x80000000.
    li      t1, 0x80000
    slli    t1, t1, 10
    li      t2, PTE_RWX
    or      t1, t1, t2
    sd      t1, 16(t3)              // index 258 * 8 = 2064

    // ---- Compose and write satp ----
    srli    t1, t0, 12              // PPN of root_pt
    li      t2, 8                   // Sv39 mode
    slli    t2, t2, 60
    or      t1, t1, t2
    sfence.vma  zero, zero
    csrw    satp, t1
    sfence.vma  zero, zero

    // ---- Jump to upper-half _high_entry ----
    // The 39-bit gap exceeds auipc+addi's signed 32-bit reach, so we
    // park the absolute upper-half VA in a 4-byte-aligned literal pool
    // immediately after the jump and load it PC-relative.
1:  auipc   t0, %pcrel_hi(.Lhigh_entry_va)
    ld      t0, %pcrel_lo(1b)(t0)
    jr      t0
.balign 8
.Lhigh_entry_va:
    .quad   _high_entry

_park_hart:
    wfi
    j       _park_hart
.size   _start, . - _start


// === Upper-half post-MMU entry =====================================

.section .text, "ax"
.balign 4
.globl _high_entry
.type  _high_entry, @function
_high_entry:
    // PC is now in the upper half; la resolves PC-relative within it.
    la      sp, __boot_stack_top

    // Zero BSS.
    la      t0, __bss_start
    la      t1, __bss_end
2:  beq     t0, t1, 3f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       2b
3:
    // a0 = hartid, a1 = dtb_phys still hold from OpenSBI handoff.
    call    croi_entry

    // croi_entry is [[noreturn]]; if it returns, halt.
4:  wfi
    j       4b
.size   _high_entry, . - _high_entry
