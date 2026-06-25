// SPDX-License-Identifier: BSD-2-Clause
//
// cara/arch.h — the CaraOS hardware-abstraction-layer (HAL) boundary
// (epic H, docs/ARCH_HAL.md). The portable Croi kernel calls these `arch_*`
// operations; exactly one backend under src/croi/arch/<arch>/ supplies them,
// chosen at build time (one ISA per image, like CARA_TARGET — no runtime
// dispatch). Today the only backend is arch/riscv64/.
//
// This is the H.1 surface: the self-contained "leaf" seams (CPU control,
// early firmware console, the free-running timer). The boot / trap / context
// switch / MMU seams move behind here in later H slices.

#ifndef CARA_ARCH_H
#define CARA_ARCH_H

#include <cara/attr.h>
#include <cara/types.h>

// ---- CPU control ----------------------------------------------------------

// Hang the hart forever (low power). Used by Croi_Halt / panic / the
// no-runnable-task scheduler terminus.
CARA_NORETURN void arch_halt(void);

// Wait for a single interrupt, then return — the idle primitive a poll loop
// uses to avoid spinning hot while waiting for a timer/device trap.
void arch_idle(void);

// Globally enable / disable taking interrupts at the kernel's privilege
// level. Individual sources still need their own enable (e.g. the timer,
// armed via arch_timer_arm). Disable is the symmetric pair; both are cheap.
void arch_irq_enable(void);
void arch_irq_disable(void);

// ---- Early firmware console ----------------------------------------------
// The byte sink used before the real UART driver is installed (and by panic,
// which must work before any printf backend). On RISC-V this is SBI.

void arch_console_putc(char c);
void arch_console_puts(const char *s);

// ---- Free-running timer ---------------------------------------------------
// The portable Croi_Time layer (src/croi/time.c) does the ns<->ticks math and
// deadline bookkeeping over these; the arch supplies the counter + compare.

// Raw monotonic tick count (RISC-V `time` CSR; counts at the platform's
// timebase frequency, which the portable layer is told via Croi_Time_Init).
u64 arch_timer_ticks(void);

// Program a one-shot timer interrupt for when the counter reaches
// `deadline_ticks`, and enable the timer interrupt source.
void arch_timer_arm(u64 deadline_ticks);

// Disable / clear the timer interrupt source. Idempotent.
void arch_timer_disarm(void);

// ---- MMU (privileged side) ------------------------------------------------
// The PTE *encoding* + the generic page-table *walk* are arch-neutral
// (<cara/arch_pte.h> + src/croi/mm/pt.c); these are the privileged ops that
// touch the live MMU. (H.2. The U-mode-entry choreography that also sets the
// page table is H.3.)
struct PageTable;

// Make `pt` the active address space: write the translation root + fence.
void arch_mmu_activate(const struct PageTable *pt);

// Full TLB fence (all addresses). For boot-time mapping changes.
void arch_mmu_fence(void);

// TLB fence for a single virtual address (a targeted invalidation).
void arch_mmu_fence_va(u64 va);

// The boot page table's root (the one currently active), as an upper-half
// VA pointer — used to install boot-time leaves into the live table.
u64 *arch_mmu_boot_root(void);

// ---- Context switch + first-dispatch (H.3) --------------------------------
// The portable scheduler drives these; the RISC-V register layout (the
// saved_int/saved_fp areas in struct Task) is the arch's. saved_int is
// TASK_NSAVED u64s, saved_fp is the FP file; their shapes are arch-defined.

// Switch the live integer + FP register context from one task to another.
// `from_int`/`from_fp` may be nullptr for the very first switch on a hart.
void arch_ctx_switch(u64 *from_int, u64 *to_int, u64 *from_fp, u64 *to_fp);

// Prime a freshly spawned task's saved register area so the first
// arch_ctx_switch into it lands correctly: a kernel task resumes in its entry
// trampoline (→ Sched_Trampoline → entry_fn); a U-mode task resumes in the
// U-mode entry path. `kstack_top` is the (16-byte-aligned) top of its kernel
// stack. The FP save area is assumed already zeroed (fresh task).
void arch_ctx_init_kernel(u64 *saved_int, u64 kstack_top);
void arch_ctx_init_user(u64 *saved_int, u64 kstack_top);

// ---- Trap + syscall ABI (H.4) ---------------------------------------------
// The arch entry builds a TrapFrame and calls the portable Croi_TrapDispatch
// (cara/trap.h), which classifies via these accessors; the syscall dispatch
// table (syscall.c) reads its args via the syscall-ABI accessors. The
// TrapFrame layout itself is arch-shaped (cara/trap.h).
struct TrapFrame;

// Install the trap vector. Call once per hart during early init.
void arch_trap_init(void);

// Classify a trap. (RISC-V: synchronous ecall-from-U vs supervisor timer
// interrupt; other arches decode their own cause register.)
bool arch_trap_is_syscall(const struct TrapFrame *frame);
bool arch_trap_is_timer(const struct TrapFrame *frame);

// Advance the saved PC past the syscall instruction (so the trap returns
// after it, not back onto it).
void arch_trap_advance(struct TrapFrame *frame);

// Dump an unhandled trap and halt.
CARA_NORETURN void arch_trap_fatal(const struct TrapFrame *frame);

// Syscall ABI: the request number, args 0..5, and where the return goes.
u64 arch_syscall_num(const struct TrapFrame *frame);
u64 arch_syscall_arg(const struct TrapFrame *frame, int i);
void arch_syscall_set_ret(struct TrapFrame *frame, i64 ret);

#endif // CARA_ARCH_H
