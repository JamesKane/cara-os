// SPDX-License-Identifier: BSD-2-Clause
//
// AArch64 early console — the arch_console_* seam (epic H.7.1, docs/ARM64.md).
//
// AArch64 has no SBI, so unlike arch/riscv64 (which leans on the SBI console
// before the NS16550 driver installs), the early/panic console is a UART at a
// known MMIO address. On `qemu-system-aarch64 -M virt` that is the PL011 at
// 0x09000000 — the de-facto early console for the virt machine. This mirrors
// how arch/riscv64 uses SBI: it is the pre-driver, panic-safe byte sink; once
// the FDT is parsed the real FDT-discovered UART driver supersedes it (no
// hardware address is hard-coded past this early window — docs/PRINCIPLES.md).

#include <cara/arch.h>
#include <cara/types.h>

// PL011 registers (ARM PrimeCell UART, 32-bit, word-indexed from the base).
static volatile u32 *const pl011 = (volatile u32 *)0x09000000UL;
#define PL011_DR (0x000 / 4)    // data register
#define PL011_FR (0x018 / 4)    // flag register
#define PL011_FR_TXFF (1u << 5) // transmit FIFO full

void arch_console_putc(char c)
{
    // Spin until the TX FIFO has room, then push the byte.
    while (pl011[PL011_FR] & PL011_FR_TXFF) {
    }
    pl011[PL011_DR] = (u32)(u8)c;
}

void arch_console_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            arch_console_putc('\r');
        }
        arch_console_putc(*s++);
    }
}
