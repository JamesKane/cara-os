// SPDX-License-Identifier: BSD-2-Clause
//
// Croi early-bringup panic and halt. Calls the SBI console directly so
// it works before any printf backend is wired.

#include "print.h"
#include "sbi.h"

#include <cara/types.h>

CARA_NORETURN void Croi_Halt(void)
{
    for (;;) {
        __asm__ volatile("wfi");
    }
}

CARA_NORETURN void Croi_Panic(const char *msg)
{
    sbi_puts("\nCROI PANIC: ");
    if (msg) {
        sbi_puts(msg);
    }
    sbi_puts("\n");
    Croi_Halt();
}
