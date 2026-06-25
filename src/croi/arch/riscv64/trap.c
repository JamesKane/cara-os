// SPDX-License-Identifier: BSD-2-Clause
//
// RISC-V trap classify + syscall-ABI extraction — the arch side of the trap
// seam (epic H, H.4). The asm entry (trap_entry.S, same dir) builds a
// TrapFrame and calls the PORTABLE Croi_TrapDispatch (src/croi/trap.c), which
// routes via the accessors here; this file owns scause decoding, the RISC-V
// syscall ABI (a7=number, a0..a5=args, a0=return), stvec install, and the
// fatal-trap dump (all of which read the RISC-V-shaped TrapFrame / CSRs).

#include <cara/arch.h>
#include <cara/trap.h> // struct TrapFrame (arch-shaped; trap_entry.S offsets match)
#include <cara/types.h>

#include "../../print.h" // Croi_Print (kernel-local early console printf)

#define SCAUSE_INTR_BIT (1ull << 63)
#define SCAUSE_CAUSE_MASK 0xffull

// ---- syscall ABI ----------------------------------------------------------

u64 arch_syscall_num(const struct TrapFrame *f)
{
    return f->x[17]; // a7
}

u64 arch_syscall_arg(const struct TrapFrame *f, int i)
{
    return f->x[10 + i]; // a0..a5 = x10..x15
}

void arch_syscall_set_ret(struct TrapFrame *f, i64 ret)
{
    f->x[10] = (u64)ret; // a0
}

// ---- trap classify --------------------------------------------------------

bool arch_trap_is_syscall(const struct TrapFrame *f)
{
    return !(f->scause & SCAUSE_INTR_BIT) && (f->scause & SCAUSE_CAUSE_MASK) == 8; // ecall from U
}

bool arch_trap_is_timer(const struct TrapFrame *f)
{
    return (f->scause & SCAUSE_INTR_BIT) && (f->scause & SCAUSE_CAUSE_MASK) == 5; // S timer
}

void arch_trap_advance(struct TrapFrame *f)
{
    f->sepc += 4; // step past the ecall
}

// ---- stvec install --------------------------------------------------------

void arch_trap_init(void)
{
    extern char croi_trap_entry[];
    u64 stvec = (u64)(uptr)croi_trap_entry; // mode 0 (direct)
    __asm__ volatile("csrw stvec, %0" : : "r"(stvec) : "memory");
}

// ---- fatal (unhandled) trap dump ------------------------------------------

// RISC-V Privileged Architecture Table 8.6 (synchronous) / 8.5 (interrupt).
static const char *exception_name(u64 cause)
{
    switch (cause & SCAUSE_CAUSE_MASK) {
    case 0:
        return "instruction address misaligned";
    case 1:
        return "instruction access fault";
    case 2:
        return "illegal instruction";
    case 3:
        return "breakpoint";
    case 4:
        return "load address misaligned";
    case 5:
        return "load access fault";
    case 6:
        return "store/AMO address misaligned";
    case 7:
        return "store/AMO access fault";
    case 8:
        return "ecall from U-mode";
    case 9:
        return "ecall from S-mode";
    case 12:
        return "instruction page fault";
    case 13:
        return "load page fault";
    case 15:
        return "store/AMO page fault";
    default:
        return "(unknown)";
    }
}

static const char *interrupt_name(u64 cause)
{
    switch (cause & SCAUSE_CAUSE_MASK) {
    case 1:
        return "supervisor software";
    case 5:
        return "supervisor timer";
    case 9:
        return "supervisor external";
    default:
        return "(unknown)";
    }
}

CARA_NORETURN void arch_trap_fatal(const struct TrapFrame *f)
{
    u64 cause = f->scause;
    if (cause & SCAUSE_INTR_BIT) {
        Croi_Print("\n*** UNHANDLED INTERRUPT ***\n"
                   "  cause:   %s (0x%llx)\n"
                   "  sepc:    0x%llx\n"
                   "  sstatus: 0x%llx\n",
                   interrupt_name(cause), cause, f->sepc, f->sstatus);
    } else {
        Croi_Print("\n*** UNHANDLED EXCEPTION ***\n"
                   "  cause:   %s (0x%llx)\n"
                   "  sepc:    0x%llx\n"
                   "  stval:   0x%llx\n"
                   "  sstatus: 0x%llx\n"
                   "  ra=0x%llx sp=0x%llx gp=0x%llx tp=0x%llx\n"
                   "  a0=0x%llx a1=0x%llx a2=0x%llx a3=0x%llx\n",
                   exception_name(cause), cause, f->sepc, f->stval, f->sstatus, f->x[1], f->x[2],
                   f->x[3], f->x[4], f->x[10], f->x[11], f->x[12], f->x[13]);
    }
    arch_halt();
}
