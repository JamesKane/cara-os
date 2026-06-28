// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 voluntary context switch — the arch_ctx_switch seam (epic H.7.4b,
// docs/ARM64.md §3). The RISC-V analogue is arch/riscv64/ctx_switch.S; the
// portable scheduler (src/croi/sched/sched.c) calls it the same way:
//
//   void arch_ctx_switch(u64 *from_int, u64 *to_int, u64 *from_fp, u64 *to_fp)
//     x0 = integer save area for the outgoing context (nullptr ⇒ skip save,
//          the very first switch on a hart)
//     x1 = integer save area to load the incoming context from
//     x2 = FP save area for the outgoing context (FP handling lands in H.7.4c)
//     x3 = FP save area to load the incoming context from
//
// Integer side: save/restore the callee-saved set (x19..x30 — x29=FP, x30=LR),
// the stack pointer, and tpidr_el1. Caller-saved registers (x0..x18) are
// clobbered by the call to arch_ctx_switch, so the compiler already spilled
// what it needs. `ret` returns to the incoming context's saved x30 — for a
// freshly primed kernel task that is its entry trampoline.
//
// Save-area layout (matches struct Task.saved_regs, CARA_ARCH_ARM64):
//   [0..11] x19..x30 @ 0..88, [12] sp @ 96, [13] tpidr_el1 @ 104.
//
// FP side (H.7.4c): the kernel is integer-only, but a U-mode task's NEON state
// lives in the hardware FP registers while it is trapped in the kernel, so we
// round-trip the *whole* file (v0..v31 + fpcr/fpsr) on every switch — without
// it a second FP-using task would clobber the first's registers. Requires
// CPACR_EL1.FPEN != 0 (set in _start.S). FP save-area layout (matches struct
// Task.fp_save, TASK_NFPSAVE=66): q0..q31 @ 0..496, fpcr @ 512, fpsr @ 520.

// FP/SIMD instructions (stp/ldp q, fpcr/fpsr) must be accepted by the
// assembler even though the kernel compiles -mgeneral-regs-only.
.arch armv8-a

.section .text, "ax"
.balign 4
.globl arch_ctx_switch
.type  arch_ctx_switch, @function
arch_ctx_switch:
    cbz     x0, .Lload              // first switch: nothing to save

    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x9, sp
    str     x9, [x0, #96]
    mrs     x9, tpidr_el1
    str     x9, [x0, #104]

    // Save the outgoing NEON file to x2 (= from_fp).
    stp     q0, q1,   [x2, #0]
    stp     q2, q3,   [x2, #32]
    stp     q4, q5,   [x2, #64]
    stp     q6, q7,   [x2, #96]
    stp     q8, q9,   [x2, #128]
    stp     q10, q11, [x2, #160]
    stp     q12, q13, [x2, #192]
    stp     q14, q15, [x2, #224]
    stp     q16, q17, [x2, #256]
    stp     q18, q19, [x2, #288]
    stp     q20, q21, [x2, #320]
    stp     q22, q23, [x2, #352]
    stp     q24, q25, [x2, #384]
    stp     q26, q27, [x2, #416]
    stp     q28, q29, [x2, #448]
    stp     q30, q31, [x2, #480]
    mrs     x9, fpcr
    str     x9, [x2, #512]
    mrs     x9, fpsr
    str     x9, [x2, #520]

.Lload:
    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]
    ldr     x9, [x1, #96]
    mov     sp, x9
    ldr     x9, [x1, #104]
    msr     tpidr_el1, x9

    // Restore the incoming NEON file from x3 (= to_fp).
    ldp     q0, q1,   [x3, #0]
    ldp     q2, q3,   [x3, #32]
    ldp     q4, q5,   [x3, #64]
    ldp     q6, q7,   [x3, #96]
    ldp     q8, q9,   [x3, #128]
    ldp     q10, q11, [x3, #160]
    ldp     q12, q13, [x3, #192]
    ldp     q14, q15, [x3, #224]
    ldp     q16, q17, [x3, #256]
    ldp     q18, q19, [x3, #288]
    ldp     q20, q21, [x3, #320]
    ldp     q22, q23, [x3, #352]
    ldp     q24, q25, [x3, #384]
    ldp     q26, q27, [x3, #416]
    ldp     q28, q29, [x3, #448]
    ldp     q30, q31, [x3, #480]
    ldr     x9, [x3, #512]
    msr     fpcr, x9
    ldr     x9, [x3, #520]
    msr     fpsr, x9

    ret
.size   arch_ctx_switch, . - arch_ctx_switch


// task_trampoline — the initial x30 (LR) for a freshly spawned kernel task
// (set by arch_ctx_init_kernel). The first arch_ctx_switch into the task `ret`s
// here; it forwards into Sched_Trampoline (C), which calls the task's entry_fn
// then Croi_TaskExit. Mirrors arch/riscv64/ctx_switch.S.
.balign 4
.globl task_trampoline
.type  task_trampoline, @function
task_trampoline:
    bl      Sched_Trampoline
    // Sched_Trampoline always exits via Croi_TaskExit; if it ever returns,
    // that is a kernel bug — fault deliberately.
    brk     #0
.size   task_trampoline, . - task_trampoline


// Tiny NEON poke/peek for the H.7.4c FP-preservation test (boot.c). The kernel
// is integer-only, so these let C set/read a NEON reg without emitting FP in C.
.balign 4
.globl arm64_fp_write_v0
.type  arm64_fp_write_v0, @function
arm64_fp_write_v0:                  // (u64 x0) → v0.d[0] = x0
    fmov    d0, x0
    ret
.size   arm64_fp_write_v0, . - arm64_fp_write_v0

.balign 4
.globl arm64_fp_read_v0
.type  arm64_fp_read_v0, @function
arm64_fp_read_v0:                   // → x0 = v0.d[0]
    fmov    x0, d0
    ret
.size   arm64_fp_read_v0, . - arm64_fp_read_v0


// A tiny EL0 user program (H.7.7e). Position-independent (PC-relative `adr` +
// immediates) so the scheduler can map these bytes at the user text VA and run
// them at EL0: it writes a line via SYS_LOG_WRITE then exits via SYS_EXIT(0x77),
// each through the AArch64 syscall ABI (x8 = number, x0..x5 args, svc #0).
// Mapped read-execute, so the message in the text page is readable too.
.balign 4
.globl arm64_el0_stub
.type  arm64_el0_stub, @function
arm64_el0_stub:
    adr     x0, .Lustub_msg              // user VA of the message
    mov     x1, #(.Lustub_msg_end - .Lustub_msg)
    mov     x8, #1                       // SYS_LOG_WRITE
    svc     #0
    mov     x0, #0x77                    // exit code
    mov     x8, #2                       // SYS_EXIT
    svc     #0
.Lustub_spin:
    b       .Lustub_spin                 // unreached: SYS_EXIT ends the task
.Lustub_msg:
    .ascii  "EL0 program ran\n"
.Lustub_msg_end:
.globl arm64_el0_stub_end
arm64_el0_stub_end:
.size   arm64_el0_stub, . - arm64_el0_stub
