// SPDX-License-Identifier: BSD-2-Clause
//
// S-mode trap dispatcher. Tier 1 only classifies and panics — async
// handling lands once the timer (Epic A.4) and PLIC (Epic B) are wired.

#include "print.h"

#include <cara/syscall.h>
#include <cara/time.h>
#include <cara/trap.h>
#include <cara/types.h>

#define SCAUSE_INTR_BIT (1ull << 63)
#define SCAUSE_CAUSE_MASK 0xffull

// RISC-V Privileged Architecture v1.13 Table 8.6 (synchronous exceptions).
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

// Asynchronous interrupt sources at S-level (Table 8.5).
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

void Croi_TrapDispatch(struct TrapFrame *frame)
{
    u64 cause = frame->scause;
    if (cause & SCAUSE_INTR_BIT) {
        switch (cause & SCAUSE_CAUSE_MASK) {
        case 5: // supervisor timer
            Croi_Time_OnTimerTrap();
            return;
        default:
            Croi_Print("\n*** UNHANDLED INTERRUPT ***\n"
                       "  cause:   %s (0x%llx)\n"
                       "  sepc:    0x%llx\n"
                       "  sstatus: 0x%llx\n",
                       interrupt_name(cause), cause, frame->sepc, frame->sstatus);
            Croi_Halt();
        }
    }

    // Synchronous exception — route ecall from U-mode to syscall dispatch.
    if ((cause & SCAUSE_CAUSE_MASK) == 8) {
        i64 ret = Croi_Syscall_Dispatch(frame);
        frame->x[10] = (u64)ret; // a0 = return value
        frame->sepc += 4;        // skip past the ecall
        return;
    }

    Croi_Print("\n*** UNHANDLED EXCEPTION ***\n"
               "  cause:   %s (0x%llx)\n"
               "  sepc:    0x%llx\n"
               "  stval:   0x%llx\n"
               "  sstatus: 0x%llx\n"
               "  ra=0x%llx sp=0x%llx gp=0x%llx tp=0x%llx\n"
               "  a0=0x%llx a1=0x%llx a2=0x%llx a3=0x%llx\n",
               exception_name(cause), cause, frame->sepc, frame->stval, frame->sstatus, frame->x[1],
               frame->x[2], frame->x[3], frame->x[4], frame->x[10], frame->x[11], frame->x[12],
               frame->x[13]);
    Croi_Halt();
}

void Croi_TrapInit(void)
{
    extern char croi_trap_entry[];
    u64 stvec = (u64)(uptr)croi_trap_entry; // mode 0 (direct), addr in upper bits
    __asm__ volatile("csrw stvec, %0" : : "r"(stvec) : "memory");
}
