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

#endif // CARA_ARCH_H
