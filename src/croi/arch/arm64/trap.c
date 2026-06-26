// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 trap classify + syscall-ABI extraction — the arch side of the trap
// seam (epic H.7.3, docs/ARM64.md §4.2). The asm entry (trap_entry.S, same dir)
// builds a TrapFrame and calls the PORTABLE Croi_TrapDispatch (src/croi/trap.c),
// which routes via the accessors here; this file owns ESR_EL1 decoding, the
// AArch64 syscall ABI (x8=number, x0..x5=args, x0=return), VBAR_EL1 install,
// and the fatal-trap dump.
//
// Differences from RISC-V worth noting:
//   - `svc` records the address of the *next* instruction in ELR, so unlike a
//     RISC-V ecall there is nothing to advance past (arch_trap_advance is a
//     no-op).
//   - sync-vs-IRQ is told by the vector group (frame->kind), not a cause bit;
//     ESR is only meaningful for synchronous exceptions, so the syscall test
//     gates on kind == sync.

#include <cara/arch.h>
#include <cara/trap.h> // struct TrapFrame (CARA_ARCH_ARM64 shape; offsets match trap_entry.S)
#include <cara/types.h>

// frame->kind tags (set by the vector stub in trap_entry.S).
enum { TRAP_KIND_SYNC = 0, TRAP_KIND_IRQ = 1, TRAP_KIND_FIQ = 2, TRAP_KIND_SERROR = 3 };

// ESR_EL1.EC (exception class), bits [31:26].
#define ESR_EC(esr) (((esr) >> 26) & 0x3full)
#define ESR_EC_SVC_A64 0x15ull // SVC instruction execution in AArch64 state

// ---- syscall ABI ----------------------------------------------------------

u64 arch_syscall_num(const struct TrapFrame *f)
{
    return f->x[8]; // x8
}

u64 arch_syscall_arg(const struct TrapFrame *f, int i)
{
    return f->x[i]; // x0..x5
}

void arch_syscall_set_ret(struct TrapFrame *f, i64 ret)
{
    f->x[0] = (u64)ret; // x0
}

// ---- trap classify --------------------------------------------------------

bool arch_trap_is_syscall(const struct TrapFrame *f)
{
    return f->kind == TRAP_KIND_SYNC && ESR_EC(f->esr) == ESR_EC_SVC_A64;
}

bool arch_trap_is_timer(const struct TrapFrame *f)
{
    // The generic-timer IRQ path (CNTV + GIC) lands in H.7.5; until then no
    // IRQ source is enabled, so nothing classifies as a timer trap.
    (void)f;
    return false;
}

void arch_trap_advance(struct TrapFrame *f)
{
    // svc leaves ELR pointing at the instruction after it — nothing to do.
    (void)f;
}

// ---- VBAR_EL1 install -----------------------------------------------------

void arch_trap_init(void)
{
    extern char arm64_vectors[];
    u64 vbar = (u64)(uptr)arm64_vectors;
    __asm__ volatile("msr vbar_el1, %0\n\tisb" : : "r"(vbar) : "memory");
}

// ---- fatal (unhandled) trap dump ------------------------------------------
// No printf backend exists this early, so dump via the arch console (mirrors
// arch_console_*). Small local hex since Croi_Print isn't linked here.

static void put_hex64(u64 v)
{
    static const char digits[] = "0123456789abcdef";
    char buf[2 + 16 + 1];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = digits[(v >> ((15 - i) * 4)) & 0xf];
    }
    buf[18] = '\0';
    arch_console_puts(buf);
}

static const char *ec_name(u64 ec)
{
    switch (ec) {
    case 0x00:
        return "unknown";
    case 0x07:
        return "SIMD/FP access trapped";
    case 0x15:
        return "SVC (AArch64)";
    case 0x18:
        return "MSR/MRS trapped";
    case 0x20:
        return "instruction abort (lower EL)";
    case 0x21:
        return "instruction abort (same EL)";
    case 0x22:
        return "PC alignment fault";
    case 0x24:
        return "data abort (lower EL)";
    case 0x25:
        return "data abort (same EL)";
    case 0x26:
        return "SP alignment fault";
    case 0x2c:
        return "trapped FP exception";
    case 0x2f:
        return "SError";
    default:
        return "(other)";
    }
}

CARA_NORETURN void arch_trap_fatal(const struct TrapFrame *f)
{
    static const char *const kind[] = { "sync", "irq", "fiq", "serror" };
    arch_console_puts("\n*** UNHANDLED AArch64 TRAP ***\n  kind:  ");
    arch_console_puts(f->kind < 4 ? kind[f->kind] : "(?)");
    arch_console_puts("\n  EC:    ");
    arch_console_puts(ec_name(ESR_EC(f->esr)));
    arch_console_puts("\n  ESR:   ");
    put_hex64(f->esr);
    arch_console_puts("\n  ELR:   ");
    put_hex64(f->elr);
    arch_console_puts("\n  SPSR:  ");
    put_hex64(f->spsr);
    arch_console_puts("\n  SP:    ");
    put_hex64(f->sp);
    arch_console_puts("\n  x0:    ");
    put_hex64(f->x[0]);
    arch_console_puts("  x1: ");
    put_hex64(f->x[1]);
    arch_console_puts("\n");
    arch_halt();
}
