// SPDX-License-Identifier: BSD-2-Clause
//
// Console input (Phase T.3.1, docs/PORTS.md §6). A raw byte ring fed by the
// NS16550 UART RX (the QEMU -nographic terminal) or by Croi_ConsoleInput_
// Inject, behind a cooked line discipline: each character is echoed raw to
// the UART, backspace erases, Enter delivers a line ('\n'-terminated). The
// dos console ACTION_READ arm drains lines via Croi_ConsoleInput_Read,
// running in the calling Process's context — so the blocking wait is just
// that Process yielding. A UART RX interrupt + a wait signal (instead of
// the poll-yield) is the refinement; v0 polls.

#include <cara/console_input.h>
#include <cara/sched.h>
#include <cara/types.h>

#include "ns16550.h"

extern struct Ns16550 g_console_uart;

// ---- raw byte ring (UART RX + injected) -----------------------------
#define RAW_CAP 256u
static char g_raw[RAW_CAP];
static u32 g_raw_head, g_raw_tail;

static void raw_push(char c)
{
    u32 next = (g_raw_tail + 1u) % RAW_CAP;
    if (next != g_raw_head) { // drop on overflow
        g_raw[g_raw_tail] = c;
        g_raw_tail = next;
    }
}

void Croi_ConsoleInput_Inject(const char *s, usize n)
{
    for (usize i = 0; i < n; i++) {
        raw_push(s[i]);
    }
}

// Next raw byte: the inject ring first, then the UART; -1 if none.
static int console_getc(void)
{
    if (g_raw_head != g_raw_tail) {
        char c = g_raw[g_raw_head];
        g_raw_head = (g_raw_head + 1u) % RAW_CAP;
        return (int)(unsigned char)c;
    }
    return ns16550_getc(&g_console_uart);
}

static void console_putc(char c)
{
    ns16550_putc(&g_console_uart, c);
}

// ---- cooked line buffer ---------------------------------------------
#define LINE_CAP 256u
static char g_line[LINE_CAP];
static usize g_line_len, g_line_pos;

// Accumulate one cooked line into g_line (echoing, with backspace editing),
// blocking (yielding) until Enter completes it. No-op if a line is still
// pending (g_line_pos < g_line_len).
static void fill_line(void)
{
    if (g_line_pos < g_line_len) {
        return;
    }
    g_line_pos = 0;
    g_line_len = 0;
    usize n = 0;
    for (;;) {
        int c = console_getc();
        if (c < 0) {
            Croi_Yield();
            continue;
        }
        if (c == '\r' || c == '\n') {
            console_putc('\r');
            console_putc('\n');
            g_line[n++] = '\n';
            g_line_len = n;
            return;
        }
        if (c == 0x08 || c == 0x7f) { // backspace / DEL
            if (n > 0) {
                n--;
                console_putc('\b');
                console_putc(' ');
                console_putc('\b');
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7f && n < LINE_CAP - 1u) {
            g_line[n++] = (char)c;
            console_putc((char)c); // echo
        }
    }
}

// Raw console output (T.3.3): a CON: handle is a terminal, so the dos
// console Write goes straight to the UART (not the decorated kernel log),
// which is what makes the boot Shell's prompt look like a prompt. '\n' is
// expanded to CR/LF for the terminal, matching the input echo.
void Croi_Console_Write(const char *buf, usize n)
{
    for (usize i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            console_putc('\r');
        }
        console_putc(c);
    }
}

usize Croi_ConsoleInput_Read(char *buf, usize len)
{
    if (len == 0) {
        return 0;
    }
    fill_line();
    usize avail = g_line_len - g_line_pos;
    usize k = len < avail ? len : avail;
    for (usize i = 0; i < k; i++) {
        buf[i] = g_line[g_line_pos + i];
    }
    g_line_pos += k;
    return k;
}
