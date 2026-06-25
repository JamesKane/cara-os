// SPDX-License-Identifier: BSD-2-Clause
//
// croi_trap_entry — S-mode trap save/restore.
//
// The sscratch register doubles as an origin marker:
//   In S-mode (kernel running)  : sscratch == 0
//   In U-mode (user running)    : sscratch == kstack_top of the running task
//
// On trap, `csrrw sp, sscratch, sp` swaps the two. After the swap:
//   * If we came from S-mode: sp now reads 0 (the kernel-mode invariant
//     that lived in sscratch); the original kernel sp is now in sscratch.
//     We swap back to recover, then proceed.
//   * If we came from U-mode: sp now points at kstack_top; the original
//     user sp is in sscratch. We restore sscratch to kstack_top so the
//     next trap from the same user task swaps correctly.
//
// On exit, we just restore everything in reverse and let `sret` return
// to whichever mode sstatus.SPP names. sscratch already holds the right
// value for that mode (0 if returning to S, kstack_top if returning to
// U), because we maintained it on entry — neither path needs a final
// fix-up.

.equ FRAME_SIZE, 288

.section .text, "ax"
.balign 4
.globl croi_trap_entry
.type  croi_trap_entry, @function
croi_trap_entry:
    csrrw   sp, sscratch, sp        // atomic swap: detect origin
    bnez    sp, .Lfrom_user

    // ---- From S-mode --------------------------------------------------
    csrrw   sp, sscratch, sp        // restore: sp = original kernel sp;
                                    //          sscratch = 0 (invariant)
    addi    sp, sp, -FRAME_SIZE
    addi    t0, sp, FRAME_SIZE      // x[2] = original kernel sp
    sd      t0, 16(sp)
    j       .Lcommon_save

.Lfrom_user:
    // ---- From U-mode --------------------------------------------------
    // sp is now kstack_top (came from sscratch); user sp is in sscratch.
    addi    sp, sp, -FRAME_SIZE
    csrr    t0, sscratch            // t0 = user sp
    sd      t0, 16(sp)              // x[2] = user sp
    addi    t0, sp, FRAME_SIZE      // restore sscratch to kstack_top
    csrw    sscratch, t0

.Lcommon_save:
    sd      x1,    8(sp)
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

    csrr    t0, sepc
    sd      t0, 256(sp)
    csrr    t0, scause
    sd      t0, 264(sp)
    csrr    t0, stval
    sd      t0, 272(sp)
    csrr    t0, sstatus
    sd      t0, 280(sp)

    mv      a0, sp
    call    Croi_TrapDispatch

    // ---- Restore CSRs (sstatus first, since SPP must be set before sret) -
    ld      t0, 256(sp)
    csrw    sepc, t0
    ld      t0, 280(sp)
    csrw    sstatus, t0

    // ---- Restore GPRs (skip x0; sp restored last via frame[2]) ----------
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

    // sp = frame[2]: the saved sp from entry. For S-mode origin that's
    // the original kernel sp; for U-mode origin that's the user sp.
    // Either way, sret returns to the right stack.
    ld      sp, 16(sp)
    sret
.size   croi_trap_entry, . - croi_trap_entry
