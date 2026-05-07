// SPDX-License-Identifier: BSD-2-Clause
//
// Croi C entry. Called from _start.S with:
//   hartid    = booting hart's hartid
//   dtb_phys  = physical address of the device tree blob (SBI convention)
//
// Phase 1 slice 1 brings the SBI console up first, parses the FDT to
// learn where the real UART lives, switches the print backend over to
// the polled NS16550 driver, prints a second hello on the new path, and
// halts. No paging, no scheduler, no IPC yet — those land in following
// slices.

#include "ns16550.h"
#include "print.h"

#include <cara/fdt.h>
#include <cara/platform.h>
#include <cara/types.h>

[[noreturn]] void croi_entry(u64 hartid, u64 dtb_phys);

static struct Ns16550 g_console_uart;

// Croi runs in the upper-half kernel VA. The boot Sv39 PT mirrors the
// lower 4 GiB into the upper half at L2[256..259], so any physical
// address P is reachable as P + KERNEL_VA_OFFSET. Anything we read from
// hardware-handed-off pointers (DTB, MMIO bases) needs this fixup.
#define KERNEL_VA_OFFSET 0xFFFFFFC000000000ULL

static inline const void *phys_to_virt(u64 phys)
{
    return (const void *)(uptr)(phys + KERNEL_VA_OFFSET);
}

static void console_putc(char c)
{
    ns16550_putc(&g_console_uart, c);
}

[[noreturn]] void croi_entry(u64 hartid, u64 dtb_phys)
{
    Croi_Print("Hello from Croi (SBI), hart=%llu dtb=0x%llx\n", hartid,
               dtb_phys);

    struct Fdt fdt;
    int rc = Fdt_Open(&fdt, phys_to_virt(dtb_phys));
    if (rc != CARA_EOK) {
        Croi_Print("Fdt_Open failed: %d\n", rc);
        Croi_Halt();
    }

    struct CroiPlatform plat = {};
    rc = Platform_FromFdt(&plat, &fdt);
    if (rc != CARA_EOK) {
        Croi_Print("Platform_FromFdt failed: %d\n", rc);
        Croi_Halt();
    }

    if (plat.console.kind != CROI_UART_NS16550) {
        Croi_Print("no NS16550 console found in FDT\n");
        Croi_Halt();
    }

    Croi_Print("console: base=0x%llx irq=%u shift=%u width=%u clock=%u baud=%u\n",
               plat.console.base, plat.console.irq, plat.console.reg_shift,
               plat.console.reg_io_width, plat.console.clock_hz,
               plat.console.baud);

    g_console_uart.base = (uptr)phys_to_virt(plat.console.base);
    g_console_uart.shift = plat.console.reg_shift;
    g_console_uart.width = plat.console.reg_io_width ? plat.console.reg_io_width : 1;
    ns16550_init(&g_console_uart, plat.console.clock_hz, plat.console.baud);

    Croi_PrintInstallBackend(console_putc);
    Croi_Print("Hello from Croi (NS16550), hart=%llu\n", hartid);

    Croi_Halt();
}
