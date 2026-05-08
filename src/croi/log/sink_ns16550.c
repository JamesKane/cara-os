// SPDX-License-Identifier: BSD-2-Clause
//
// LogSink that emits records over an NS16550 UART. Buffers a formatted
// line on the stack, then writes byte-by-byte through the polled
// driver. Owns no state itself — the global ns16550 handle is
// supplied via the LogSink ctx.

#include <cara/log.h>
#include <cara/types.h>

// Forward-declare what we need so this TU doesn't depend on the
// kernel-only ns16550.h relative path.
struct Ns16550;
void ns16550_putc(struct Ns16550 *u, char c);

void Log_Sink_NS16550_Emit(const struct LogRecord *r, void *ctx)
{
    struct Ns16550 *uart = (struct Ns16550 *)ctx;
    if (!uart) {
        return;
    }
    char buf[CARA_LOG_RECORD_BYTES + 64];
    usize n = Log_FormatHuman(buf, sizeof(buf), r);
    for (usize i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            ns16550_putc(uart, '\r');
        }
        ns16550_putc(uart, c);
    }
}
