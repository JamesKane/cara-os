// SPDX-License-Identifier: BSD-2-Clause
//
// Console input (Phase T.3.1) — a cooked, line-disciplined byte source for
// the dos console handle, so a Process's Read(Input()) returns typed input.
// Bytes come from the NS16550 UART RX (the QEMU -nographic terminal) or from
// Croi_ConsoleInput_Inject (a test / future feed). The dos ACTION_READ
// console arm calls Croi_ConsoleInput_Read in the caller's context, so the
// blocking wait is the calling Process yielding (docs/PORTS.md §6).

#ifndef CARA_CONSOLE_INPUT_H
#define CARA_CONSOLE_INPUT_H

#include <cara/types.h>

// Read up to `len` bytes of cooked console input into `buf`. A line is
// echoed as it is typed, with backspace editing, and delivered on Enter
// (terminated by '\n'). Blocks (yields the caller) until a line is
// available; a read may return part of a line, the rest on the next call.
// Returns the number of bytes read (> 0).
usize Croi_ConsoleInput_Read(char *buf, usize len);

// Feed raw bytes into the console input as if typed — the UART RX poll and
// the kernel test both use this.
void Croi_ConsoleInput_Inject(const char *s, usize n);

// Write raw bytes to the console (the UART terminal), expanding '\n' to
// CR/LF. This backs the dos CON: handle's Write — a console is a raw
// terminal, not the decorated kernel log (T.3.3).
void Croi_Console_Write(const char *buf, usize n);

#endif // CARA_CONSOLE_INPUT_H
