// SPDX-License-Identifier: BSD-2-Clause
//
// Voluntary kernel-thread context switch.
//
//   void croi_ctx_switch(u64 *from_int, u64 *to_int, u64 *from_fp, u64 *to_fp)
//     a0 = 17-u64 integer save area for the outgoing context
//          (nullptr for the very first switch on a hart → skip save)
//     a1 = 17-u64 integer save area to load the incoming context from
//     a2 = 33-u64 FP save area for the outgoing context (nullptr ⇒ a0 null)
//     a3 = 33-u64 FP save area to load the incoming context from
//
// Integer side: save ra / sp / gp / tp / s0..s11 (the callee-saved subset)
// plus sscratch. Caller-saved integer registers are clobbered by the call
// to croi_ctx_switch, so the compiler already spilled what it needs.
//
// FP side (T.4.1): the kernel is soft-float, so a U-mode task's FP state is
// NOT in the trap frame — it lives in the hardware FP registers while the
// task is trapped in the kernel. So we save/restore the *whole* file
// (f0..f31 + fcsr), unconditionally, on every switch — without this, a
// second FP-using task would clobber the first's registers. Requires
// sstatus.FS != Off, which _start.S sets at boot and keeps set.
//
// task_trampoline is the initial `ra` for a freshly spawned task. The
// scheduler hands control to it; it forwards into Sched_Trampoline (C)
// which calls entry_fn(entry_arg) and then Croi_TaskExit.

.section .text, "ax"
.balign 4
.globl croi_ctx_switch
.type  croi_ctx_switch, @function
croi_ctx_switch:
    beqz    a0, .Lload                      // first switch: skip save

    sd      ra,    0(a0)
    sd      sp,    8(a0)
    sd      gp,   16(a0)
    sd      tp,   24(a0)
    sd      s0,   32(a0)
    sd      s1,   40(a0)
    sd      s2,   48(a0)
    sd      s3,   56(a0)
    sd      s4,   64(a0)
    sd      s5,   72(a0)
    sd      s6,   80(a0)
    sd      s7,   88(a0)
    sd      s8,   96(a0)
    sd      s9,  104(a0)
    sd      s10, 112(a0)
    sd      s11, 120(a0)

    csrr    t0, sscratch                    // saved_regs[16] = sscratch
    sd      t0, 128(a0)

    // Save outgoing FP file (f0..f31 at 0..248, fcsr at 256) to a2.
    fsd     f0,    0(a2)
    fsd     f1,    8(a2)
    fsd     f2,   16(a2)
    fsd     f3,   24(a2)
    fsd     f4,   32(a2)
    fsd     f5,   40(a2)
    fsd     f6,   48(a2)
    fsd     f7,   56(a2)
    fsd     f8,   64(a2)
    fsd     f9,   72(a2)
    fsd     f10,  80(a2)
    fsd     f11,  88(a2)
    fsd     f12,  96(a2)
    fsd     f13, 104(a2)
    fsd     f14, 112(a2)
    fsd     f15, 120(a2)
    fsd     f16, 128(a2)
    fsd     f17, 136(a2)
    fsd     f18, 144(a2)
    fsd     f19, 152(a2)
    fsd     f20, 160(a2)
    fsd     f21, 168(a2)
    fsd     f22, 176(a2)
    fsd     f23, 184(a2)
    fsd     f24, 192(a2)
    fsd     f25, 200(a2)
    fsd     f26, 208(a2)
    fsd     f27, 216(a2)
    fsd     f28, 224(a2)
    fsd     f29, 232(a2)
    fsd     f30, 240(a2)
    fsd     f31, 248(a2)
    frcsr   t0
    sd      t0,  256(a2)

.Lload:
    ld      ra,    0(a1)
    ld      sp,    8(a1)
    ld      gp,   16(a1)
    ld      tp,   24(a1)
    ld      s0,   32(a1)
    ld      s1,   40(a1)
    ld      s2,   48(a1)
    ld      s3,   56(a1)
    ld      s4,   64(a1)
    ld      s5,   72(a1)
    ld      s6,   80(a1)
    ld      s7,   88(a1)
    ld      s8,   96(a1)
    ld      s9,  104(a1)
    ld      s10, 112(a1)
    ld      s11, 120(a1)

    ld      t0, 128(a1)
    csrw    sscratch, t0

    // Restore incoming FP file from a3.
    fld     f0,    0(a3)
    fld     f1,    8(a3)
    fld     f2,   16(a3)
    fld     f3,   24(a3)
    fld     f4,   32(a3)
    fld     f5,   40(a3)
    fld     f6,   48(a3)
    fld     f7,   56(a3)
    fld     f8,   64(a3)
    fld     f9,   72(a3)
    fld     f10,  80(a3)
    fld     f11,  88(a3)
    fld     f12,  96(a3)
    fld     f13, 104(a3)
    fld     f14, 112(a3)
    fld     f15, 120(a3)
    fld     f16, 128(a3)
    fld     f17, 136(a3)
    fld     f18, 144(a3)
    fld     f19, 152(a3)
    fld     f20, 160(a3)
    fld     f21, 168(a3)
    fld     f22, 176(a3)
    fld     f23, 184(a3)
    fld     f24, 192(a3)
    fld     f25, 200(a3)
    fld     f26, 208(a3)
    fld     f27, 216(a3)
    fld     f28, 224(a3)
    fld     f29, 232(a3)
    fld     f30, 240(a3)
    fld     f31, 248(a3)
    ld      t0,  256(a3)
    fscsr   t0

    ret
.size   croi_ctx_switch, . - croi_ctx_switch


.balign 4
.globl task_trampoline
.type  task_trampoline, @function
task_trampoline:
    call    Sched_Trampoline
    // Sched_Trampoline always exits via Croi_TaskExit; if it ever
    // returns, that's a kernel bug — fault deliberately.
    unimp
.size   task_trampoline, . - task_trampoline
