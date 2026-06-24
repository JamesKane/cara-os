// SPDX-License-Identifier: BSD-2-Clause

#include "ns16550.h"

#include <cara/types.h>

enum {
    NS_THR = 0, // Transmit Holding (DLAB=0)
    NS_RBR = 0, // Receive Buffer (DLAB=0)
    NS_IER = 1, // Interrupt Enable (DLAB=0)
    NS_DLL = 0, // Divisor Latch Low (DLAB=1)
    NS_DLM = 1, // Divisor Latch High (DLAB=1)
    NS_FCR = 2, // FIFO Control (write)
    NS_LCR = 3, // Line Control
    NS_MCR = 4, // Modem Control
    NS_LSR = 5, // Line Status (read-only)
};

#define LCR_DLAB 0x80
#define LCR_8N1 0x03
#define FCR_FIFO_EN 0x07 // enable FIFO + clear RX/TX
#define LSR_THRE 0x20    // transmit holding register empty
#define LSR_DR 0x01      // received data ready

static u8 ns_read(const struct Ns16550 *u, u32 reg)
{
    uptr addr = u->base + ((uptr)reg << u->shift);
    if (u->width == 4) {
        return (u8)(*(volatile u32 *)addr);
    }
    return *(volatile u8 *)addr;
}

static void ns_write(const struct Ns16550 *u, u32 reg, u8 v)
{
    uptr addr = u->base + ((uptr)reg << u->shift);
    if (u->width == 4) {
        *(volatile u32 *)addr = (u32)v;
    } else {
        *(volatile u8 *)addr = v;
    }
}

void ns16550_init(struct Ns16550 *u, u32 clock_hz, u32 baud)
{
    if (u->width != 4) {
        u->width = 1;
    }

    if (clock_hz != 0 && baud != 0) {
        u32 div = clock_hz / (16u * baud);
        if (div == 0) {
            div = 1;
        }
        ns_write(u, NS_LCR, LCR_DLAB);
        ns_write(u, NS_DLL, (u8)(div & 0xFFu));
        ns_write(u, NS_DLM, (u8)((div >> 8) & 0xFFu));
    }
    ns_write(u, NS_LCR, LCR_8N1);
    ns_write(u, NS_IER, 0x00); // polled mode
    ns_write(u, NS_FCR, FCR_FIFO_EN);
    ns_write(u, NS_MCR, 0x03); // DTR + RTS
}

void ns16550_putc(struct Ns16550 *u, char c)
{
    while ((ns_read(u, NS_LSR) & LSR_THRE) == 0) {
        // spin
    }
    ns_write(u, NS_THR, (u8)c);
}

bool ns16550_rx_ready(const struct Ns16550 *u)
{
    return (ns_read(u, NS_LSR) & LSR_DR) != 0;
}

int ns16550_getc(struct Ns16550 *u)
{
    if ((ns_read(u, NS_LSR) & LSR_DR) == 0) {
        return -1;
    }
    return (int)ns_read(u, NS_RBR);
}
