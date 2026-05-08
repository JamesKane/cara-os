// SPDX-License-Identifier: BSD-2-Clause
//
// croi_trap_entry — S-mode trap save/restore.
//
// Layout matches struct TrapFrame in include/cara/trap.h exactly:
//
//     0      x[0]    (reserved, kept zero)
//     8      x[1]    (ra)
//    16      x[2]    (sp at trap entry, fixed up after the bulk save)
//    24      x[3]    (gp)
//    ...
//   248      x[31]
//   256      sepc
//   264      scause
//   272      stval
//   280      sstatus
//
// Total 288 bytes. Tier 1 does not swap sp via sscratch — every trap
// here comes from S-mode and we already hold a kernel stack. When U-mode
// arrives (Epic K) this entry will check sscratch and conditionally
// swap. Until then, sscratch is left at 0 as a kernel-mode invariant
// marker, in line with VectraOS's pattern.

.equ FRAME_SIZE, 288

.section .text, "ax"
.balign 4                       // stvec mode 0 (direct) requires 4-byte align
.globl croi_trap_entry
.type  croi_trap_entry, @function
croi_trap_entry:
    addi    sp, sp, -FRAME_SIZE

    // Save GPRs. x0 always reads 0; we leave its slot at zero.
    sd      x1,    8(sp)
    sd      x2,   16(sp)        // patched below to original sp
    sd      x3,   24(sp)
    sd      x4,   32(sp)
    sd      x5,   40(sp)
    sd      x6,   48(sp)
    sd      x7,   56(sp)
    sd      x8,   64(sp)
    sd      x9,   72(sp)
    sd      x10,  80(sp)
    sd      x11,  88(sp)
    sd      x12,  96(sp)
    sd      x13, 104(sp)
    sd      x14, 112(sp)
    sd      x15, 120(sp)
    sd      x16, 128(sp)
    sd      x17, 136(sp)
    sd      x18, 144(sp)
    sd      x19, 152(sp)
    sd      x20, 160(sp)
    sd      x21, 168(sp)
    sd      x22, 176(sp)
    sd      x23, 184(sp)
    sd      x24, 192(sp)
    sd      x25, 200(sp)
    sd      x26, 208(sp)
    sd      x27, 216(sp)
    sd      x28, 224(sp)
    sd      x29, 232(sp)
    sd      x30, 240(sp)
    sd      x31, 248(sp)

    // Patch x[2] slot with original sp.
    addi    t0, sp, FRAME_SIZE
    sd      t0, 16(sp)

    // Save trap CSRs.
    csrr    t0, sepc
    sd      t0, 256(sp)
    csrr    t0, scause
    sd      t0, 264(sp)
    csrr    t0, stval
    sd      t0, 272(sp)
    csrr    t0, sstatus
    sd      t0, 280(sp)

    // Dispatch.
    mv      a0, sp
    call    Croi_TrapDispatch

    // Restore CSRs (sscratch left as-is).
    ld      t0, 256(sp)
    csrw    sepc, t0
    ld      t0, 280(sp)
    csrw    sstatus, t0

    // Restore GPRs (skip x0; skip x2 — sp restored by addi at the end).
    ld      x1,    8(sp)
    ld      x3,   24(sp)
    ld      x4,   32(sp)
    ld      x5,   40(sp)
    ld      x6,   48(sp)
    ld      x7,   56(sp)
    ld      x8,   64(sp)
    ld      x9,   72(sp)
    ld      x10,  80(sp)
    ld      x11,  88(sp)
    ld      x12,  96(sp)
    ld      x13, 104(sp)
    ld      x14, 112(sp)
    ld      x15, 120(sp)
    ld      x16, 128(sp)
    ld      x17, 136(sp)
    ld      x18, 144(sp)
    ld      x19, 152(sp)
    ld      x20, 160(sp)
    ld      x21, 168(sp)
    ld      x22, 176(sp)
    ld      x23, 184(sp)
    ld      x24, 192(sp)
    ld      x25, 200(sp)
    ld      x26, 208(sp)
    ld      x27, 216(sp)
    ld      x28, 224(sp)
    ld      x29, 232(sp)
    ld      x30, 240(sp)
    ld      x31, 248(sp)

    addi    sp, sp, FRAME_SIZE
    sret
.size   croi_trap_entry, . - croi_trap_entry
