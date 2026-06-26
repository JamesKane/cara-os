// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 trap entry — VBAR_EL1 vector table + save/restore (epic H.7.3,
// docs/ARM64.md §4.2). The RISC-V analogue is arch/riscv64/trap_entry.S; the
// shapes differ:
//   - AArch64 has a 16-entry vector table (4 exception kinds × 4 source states),
//     each slot 0x80 bytes, base in VBAR_EL1 (set by arch_trap_init). The CPU
//     auto-selects SP_EL1 on entry to EL1h, so there is no sscratch dance —
//     SP_EL1 is the kernel stack (the running kernel stack for an EL1 origin,
//     the task's kernel stack for an EL0 origin, set before the eret to EL0).
//   - Sync-vs-IRQ is told by *which vector* fired, not a cause bit, so each
//     vector tags the frame with a synthetic `kind` (0=sync 1=irq 2=fiq
//     3=serror) for the portable dispatcher.
//
// Each vector saves x0/x1, loads the kind into x0, and joins the common path,
// which saves the rest of the frame and calls the portable Croi_TrapDispatch.
// Frame layout matches struct TrapFrame (cara/trap.h, CARA_ARCH_ARM64):
//   x0..x30 @ 0..240, sp @ 248, elr @ 256, spsr @ 264, esr @ 272, kind @ 280.

.equ FRAME_SIZE, 288

.macro VEC kind
    sub     sp, sp, #FRAME_SIZE
    stp     x0, x1, [sp, #0]
    mov     x0, #\kind
    b       arm64_trap_common
.endm

.section .text, "ax"

// VBAR_EL1 requires the table to be 2 KiB aligned; each vector is 0x80 apart.
.balign 0x800
.globl arm64_vectors
.type  arm64_vectors, @function
arm64_vectors:
    // ---- Current EL with SP0 (unused: the kernel runs EL1h/SPx) ----
    VEC 0
    .balign 0x80
    VEC 1
    .balign 0x80
    VEC 2
    .balign 0x80
    VEC 3
    .balign 0x80
    // ---- Current EL with SPx (kernel-origin traps; EL1 svc lands here) ----
    VEC 0
    .balign 0x80
    VEC 1
    .balign 0x80
    VEC 2
    .balign 0x80
    VEC 3
    .balign 0x80
    // ---- Lower EL using AArch64 (EL0-origin traps; U-mode, from H.7.4) ----
    VEC 0
    .balign 0x80
    VEC 1
    .balign 0x80
    VEC 2
    .balign 0x80
    VEC 3
    .balign 0x80
    // ---- Lower EL using AArch32 (CaraOS has no AArch32 U-mode) ----
    VEC 0
    .balign 0x80
    VEC 1
    .balign 0x80
    VEC 2
    .balign 0x80
    VEC 3
.size   arm64_vectors, . - arm64_vectors

.balign 4
.type  arm64_trap_common, @function
arm64_trap_common:
    // x0/x1 already saved by the vector; x0 now holds `kind`.
    stp     x2, x3,   [sp, #16]
    stp     x4, x5,   [sp, #32]
    stp     x6, x7,   [sp, #48]
    stp     x8, x9,   [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30,      [sp, #240]
    str     x0,       [sp, #280]    // kind

    mrs     x10, elr_el1
    str     x10,      [sp, #256]
    mrs     x9, spsr_el1
    str     x9,       [sp, #264]
    mrs     x11, esr_el1
    str     x11,      [sp, #272]

    // Pre-trap SP: SPSR.M[3:0] == 0 ⇒ EL0t (user SP in SP_EL0); else EL1.
    and     x12, x9, #0xf
    cbz     x12, .Lfrom_el0
    add     x13, sp, #FRAME_SIZE
    b       .Lsp_done
.Lfrom_el0:
    mrs     x13, sp_el0
.Lsp_done:
    str     x13,      [sp, #248]

    mov     x0, sp
    bl      Croi_TrapDispatch

    // ---- Restore ----
    ldr     x10, [sp, #256]
    msr     elr_el1, x10
    ldr     x9, [sp, #264]
    msr     spsr_el1, x9
    ldr     x13, [sp, #248]
    msr     sp_el0, x13              // meaningful for an EL0 return; harmless else

    ldp     x2, x3,   [sp, #16]
    ldp     x4, x5,   [sp, #32]
    ldp     x6, x7,   [sp, #48]
    ldp     x8, x9,   [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30,      [sp, #240]
    ldp     x0, x1,   [sp, #0]
    add     sp, sp, #FRAME_SIZE
    eret
.size   arm64_trap_common, . - arm64_trap_common
