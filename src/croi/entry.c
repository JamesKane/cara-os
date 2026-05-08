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

#include <cara/alloc.h>
#include <cara/fdt.h>
#include <cara/log.h>
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

    // ---- Time first, so subsequent log records have meaningful timestamps.
    if (plat.sstc_present && plat.timebase_hz != 0) {
        Croi_Time_Init(plat.timebase_hz);
    }

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

    // ---- Page allocator ----
    static struct PageAllocator g_page_alloc;
    rc = Page_Init(&g_page_alloc, &pm);
    if (rc != CARA_EOK) {
        Croi_Print("Page_Init failed: %d\n", rc);
        Croi_Halt();
    }

    // Smoke: alloc 16 single pages, ensure they're distinct, free all,
    // verify counter restore.
    {
        u64 saved_free = g_page_alloc.free_pages;
        u64 pages[16];
        for (u32 i = 0; i < 16; i++) {
            pages[i] = Page_Alloc(&g_page_alloc, 1);
            if (pages[i] == 0) {
                Croi_Print("Page_Alloc smoke: alloc %u failed\n", i);
                Croi_Halt();
            }
            for (u32 j = 0; j < i; j++) {
                if (pages[j] == pages[i]) {
                    Croi_Print("Page_Alloc smoke: duplicate at %u/%u (0x%llx)\n",
                               j, i, pages[i]);
                    Croi_Halt();
                }
            }
        }
        if (g_page_alloc.free_pages != saved_free - 16) {
            Croi_Print("Page_Alloc smoke: counter wrong after 16 allocs\n");
            Croi_Halt();
        }
        for (u32 i = 0; i < 16; i++) {
            Page_Free(&g_page_alloc, pages[i], 1);
        }
        if (g_page_alloc.free_pages != saved_free) {
            Croi_Print("Page_Alloc smoke: counter not restored after free\n");
            Croi_Halt();
        }
        // 4-page contiguous alloc.
        u64 multi = Page_Alloc(&g_page_alloc, 4);
        if (multi == 0 || (multi & 0xFFF) != 0) {
            Croi_Print("Page_Alloc smoke: 4-page alloc failed (0x%llx)\n", multi);
            Croi_Halt();
        }
        Page_Free(&g_page_alloc, multi, 4);
        if (g_page_alloc.free_pages != saved_free) {
            Croi_Print("Page_Alloc smoke: counter wrong after multi-free\n");
            Croi_Halt();
        }
        Croi_Print("page alloc smoke: PASS (peak in-flight = %llu pages)\n",
                   g_page_alloc.peak_in_flight_pages);
    }

    // ---- Kernel heap ----
    static struct Heap g_heap;
    rc = Heap_Init(&g_heap, &g_page_alloc);
    if (rc != CARA_EOK) {
        Croi_Print("Heap_Init failed: %d\n", rc);
        Croi_Halt();
    }
    Heap_SetActive(&g_heap);

    // ---- Structured logging on top of the heap ----
    if (Log_Init() != CARA_EOK) {
        Croi_Print("Log_Init failed\n");
        Croi_Halt();
    }
    struct LogSink uart_sink = {
        .emit = Log_Sink_NS16550_Emit,
        .ctx = &g_console_uart,
        .ansi_capable = true,
        .min_level = (u8)LOG_LV_TRACE,
    };
    if (Log_RegisterSink(&uart_sink) != CARA_EOK) {
        Croi_Print("Log_RegisterSink failed\n");
        Croi_Halt();
    }

    // From here forward, prefer LOG_* over Croi_Print so output is
    // captured in the ring and routed through every active sink.
    LOG_INFO("boot", "CaraOS Croi up at upper-half VAs (hart=%llu)", hartid);
    LOG_INFO("mem ", "kernel image: 0x%llx..0x%llx (%llu KiB)", kphys_start,
             kphys_end, (kphys_end - kphys_start) / 1024);
    LOG_INFO("mem ", "dtb:          0x%llx..0x%llx (%llu bytes)", dtb_start,
             dtb_end, dtb_end - dtb_start);
    LOG_INFO("mem ", "phys: %llu MiB across %u banks; usable %llu MiB across %u runs",
             pm.total_bytes / (1024 * 1024), pm.n_banks,
             pm.usable_bytes / (1024 * 1024), pm.n_usable);
    LOG_INFO("mem ", "page allocator: %llu pages free", g_page_alloc.free_pages);
    LOG_INFO("mem ", "heap: %u classes, ready", CARA_HEAP_NUM_CLASSES);
    LOG_INFO("uart", "NS16550 base=0x%llx irq=%u shift=%u width=%u",
             plat.console.base, plat.console.irq, plat.console.reg_shift,
             plat.console.reg_io_width);
    LOG_INFO("time", "timebase=%llu Hz, sstc=%s", plat.timebase_hz,
             plat.sstc_present ? "present" : "absent");

    for (u32 i = 0; i < pm.n_usable; i++) {
        LOG_DEBUG("mem ", "usable[%u]: 0x%llx..0x%llx (%llu KiB)", i,
                  pm.usable[i].base, pm.usable[i].base + pm.usable[i].size,
                  pm.usable[i].size / 1024);
    }
    LOG_INFO("test", "page-alloc smoke: peak %llu pages",
             g_page_alloc.peak_in_flight_pages);
    LOG_INFO("test", "heap smoke:       peak %llu bytes (large_peak=%u)",
             g_heap.peak_bytes_in_flight, g_heap.large_peak);

    // Smoke: alloc/free across every size class plus a large alloc.
    {
        const usize sizes[] = { 8, 16, 17, 32, 100, 256, 1000, 2048, 4096, 16384 };
        const u32 n = sizeof(sizes) / sizeof(sizes[0]);
        void *ptrs[16];
        u64 bytes_before = g_heap.bytes_in_flight;
        for (u32 i = 0; i < n; i++) {
            ptrs[i] = Croi_Alloc(sizes[i]);
            if (!ptrs[i]) {
                Croi_Print("Heap smoke: Croi_Alloc(%llu) failed\n",
                           (u64)sizes[i]);
                Croi_Halt();
            }
            // Touch the memory to confirm we got a usable pointer.
            for (u32 k = 0; k < (u32)sizes[i]; k++) {
                ((u8 *)ptrs[i])[k] = (u8)(k & 0xFF);
            }
        }
        for (u32 i = 0; i < n; i++) {
            Croi_Free(ptrs[i]);
        }
        if (g_heap.bytes_in_flight != bytes_before) {
            Croi_Print("Heap smoke: bytes_in_flight not restored (%llu vs %llu)\n",
                       g_heap.bytes_in_flight, bytes_before);
            Croi_Halt();
        }
        // Also stress one slab class to force a slab-page grow.
        void *many[200];
        for (u32 i = 0; i < 200; i++) {
            many[i] = Croi_Alloc(64);
            if (!many[i]) {
                Croi_Print("Heap smoke: 64-byte alloc %u failed\n", i);
                Croi_Halt();
            }
        }
        for (u32 i = 0; i < 200; i++) {
            Croi_Free(many[i]);
        }
        if (g_heap.bytes_in_flight != bytes_before) {
            Croi_Print("Heap smoke: bytes_in_flight wrong after stress\n");
            Croi_Halt();
        }
        Croi_Print("heap smoke: PASS (peak %llu bytes in-flight, large_peak=%u)\n",
                   g_heap.peak_bytes_in_flight, g_heap.large_peak);
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
