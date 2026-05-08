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
#include <cara/mm.h>
#include <cara/platform.h>
#include <cara/time.h>
#include <cara/trap.h>
#include <cara/types.h>

// Kernel-image extents materialised into upper-half rodata by the
// linker script's .kernel_extents — see croi.lds. We can't reach the
// low-phys symbols directly from upper-half code (39-bit gap).
extern u64 _kernel_image_phys_start;
extern u64 _kernel_image_phys_end;

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
    Croi_TrapInit();

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

    // ---- Physical memory map ----
    u64 kphys_start = _kernel_image_phys_start;
    u64 kphys_end = _kernel_image_phys_end;
    u64 dtb_start = dtb_phys;
    u64 dtb_end = dtb_phys + fdt.totalsize;

    struct PhysMap pm;
    rc = Mm_PhysMapFromFdt(&pm, &fdt, kphys_start, kphys_end, dtb_start, dtb_end);
    if (rc != CARA_EOK) {
        Croi_Print("Mm_PhysMapFromFdt failed: %d\n", rc);
        Croi_Halt();
    }

    Croi_Print("kernel image: 0x%llx..0x%llx (%llu KiB)\n", kphys_start, kphys_end,
               (kphys_end - kphys_start) / 1024);
    Croi_Print("dtb:          0x%llx..0x%llx (%llu bytes)\n", dtb_start, dtb_end,
               dtb_end - dtb_start);
    Croi_Print("phys banks:   %u, total %llu MiB\n", pm.n_banks,
               pm.total_bytes / (1024 * 1024));
    for (u32 i = 0; i < pm.n_banks; i++) {
        Croi_Print("  bank[%u]: 0x%llx..0x%llx (%llu MiB)\n", i, pm.bank[i].base,
                   pm.bank[i].base + pm.bank[i].size,
                   pm.bank[i].size / (1024 * 1024));
    }
    Croi_Print("usable runs:  %u, total %llu MiB\n", pm.n_usable,
               pm.usable_bytes / (1024 * 1024));
    for (u32 i = 0; i < pm.n_usable; i++) {
        Croi_Print("  run[%u]:  0x%llx..0x%llx (%llu KiB)\n", i, pm.usable[i].base,
                   pm.usable[i].base + pm.usable[i].size,
                   pm.usable[i].size / 1024);
    }

    if (!plat.sstc_present || plat.timebase_hz == 0) {
        Croi_Print("Sstc not present (timebase=%llu); skipping timer demo\n",
                   plat.timebase_hz);
        Croi_Halt();
    }

    Croi_Time_Init(plat.timebase_hz);
    Croi_Print("timebase=%llu Hz, time_now=%llu ns\n", plat.timebase_hz,
               Croi_Time_Now());

    // 100 ms one-shot timer demo. Verify by elapsed measurement.
    const u64 deadline_ns = 100ull * 1000ull * 1000ull;
    const u64 t0 = Croi_Time_Now();
    Croi_Time_SetDeadline(t0 + deadline_ns);
    while (!Croi_Time_DeadlineFired()) {
        __asm__ volatile("wfi");
    }
    const u64 elapsed = Croi_Time_Now() - t0;
    Croi_Print("[timer] fired after %llu ns (target %llu ns)\n", elapsed,
               deadline_ns);

    Croi_Halt();
}
