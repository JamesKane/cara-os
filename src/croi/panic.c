// SPDX-License-Identifier: BSD-2-Clause
//
// Croi early-bringup panic and halt. Uses the arch early firmware console
// (arch_console_puts) so it works before any printf backend is wired, and
// arch_halt to hang the hart.

#include <cara/arch.h>
#include <cara/types.h>

CARA_NORETURN void Croi_Halt(void)
{
    arch_halt();
}

CARA_NORETURN void Croi_Panic(const char *msg)
{
    arch_console_puts("\nCROI PANIC: ");
    if (msg) {
        arch_console_puts(msg);
    }
    arch_console_puts("\n");
    Croi_Halt();
}
