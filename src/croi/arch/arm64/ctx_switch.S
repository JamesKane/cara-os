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
// FP (NEON v0..v31 + fpcr/fpsr) is NOT yet saved here — kernel tasks are
// integer-only (-mgeneral-regs-only), so a kernel→kernel switch needs no FP
// round-trip. U-mode FP save/restore (and CPACR_EL1) arrive with enter-U-mode
// in H.7.4c, at which point x2/x3 are consumed.

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

    ret
.size   arch_ctx_switch, . - arch_ctx_switch
