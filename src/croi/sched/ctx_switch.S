// SPDX-License-Identifier: BSD-2-Clause
//
// Voluntary kernel-thread context switch.
//
//   void croi_ctx_switch(u64 *from, u64 *to)
//     a0 = pointer to 16-u64 save area for the outgoing context
//          (may be nullptr for the very first switch on a hart)
//     a1 = pointer to 16-u64 save area to load the incoming context from
//
// We save ra / sp / gp / tp / s0..s11 — the callee-saved subset, plus
// the link register and stack pointer that define the suspended frame.
// Caller-saved registers are clobbered by the call sequence to
// croi_ctx_switch itself, so the compiler has already spilled anything
// it needs around the call.
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
