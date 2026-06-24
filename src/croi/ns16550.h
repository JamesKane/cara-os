// SPDX-License-Identifier: BSD-2-Clause
//
// Polled NS16550 UART driver. FDT-driven so the same driver handles QEMU
// virt's 8-bit-stride UART and any other ns16550-family chip with the
// reg-shift / reg-io-width quirks that come with it.

#ifndef CARA_CROI_NS16550_H
#define CARA_CROI_NS16550_H

#include <cara/types.h>

struct Ns16550 {
    uptr base; // MMIO base (any addressable u8/u32 address)
    u32 shift; // reg-shift from FDT (0 for 8-bit, 2 for 32-bit lanes)
    u32 width; // reg-io-width from FDT (1 or 4)
};

void ns16550_init(struct Ns16550 *u, u32 clock_hz, u32 baud);
void ns16550_putc(struct Ns16550 *u, char c);

// True when a received byte is waiting (LSR data-ready bit).
bool ns16550_rx_ready(const struct Ns16550 *u);

// Non-blocking receive: the next byte (0..255), or -1 if none is ready.
int ns16550_getc(struct Ns16550 *u);

#endif
