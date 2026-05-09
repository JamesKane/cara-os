// SPDX-License-Identifier: BSD-2-Clause
//
// struct CroiPlatform — the FDT-derived snapshot of "what hardware is on
// this board." Populated by Platform_FromFdt() during early Croi init and
// then read by every subsequent module.
//
// Phase 1 slice 1 only fills the console UART fields; later slices fill
// the rest as they need it.

#ifndef CARA_PLATFORM_H
#define CARA_PLATFORM_H

#include <cara/fdt.h>
#include <cara/types.h>

enum CroiUartKind : u32 {
    CROI_UART_NONE = 0,
    CROI_UART_NS16550 = 1,
    CROI_UART_PXA = 2, // X1 / OrangePi RV2; not used in slice 1
};

struct CroiUart {
    enum CroiUartKind kind;
    u64 base;
    u32 irq;
    u32 reg_shift;
    u32 reg_io_width;
    u32 clock_hz;
    u32 baud;
};

struct CroiPlatform {
    struct CroiUart console;
    u64 timebase_hz;   // /cpus/timebase-frequency
    bool sstc_present; // /cpus/cpu@0/riscv,isa contains "_sstc" or "sstc"
    // Memory banks, CLINT, PLIC, per-hart ISA bitmaps land in later slices.
};

[[nodiscard]] int Platform_FromFdt(struct CroiPlatform *out, const struct Fdt *fdt);

#endif
