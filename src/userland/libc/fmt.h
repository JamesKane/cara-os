// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS userland libc — the internal printf core interface (Phase T.1).
// One vformat() drives every formatted-output entry point via a sink: the
// buffer sinks (s(n)printf use buf/cap/pos) and the stream sink (printf/
// fprintf, stdio.c, uses buf/pos as a flush chunk). Not a public header.

#ifndef CARA_LIBC_FMT_H
#define CARA_LIBC_FMT_H

#include <stdarg.h>
#include <stddef.h>

struct cara_fmt_out {
    void (*write)(struct cara_fmt_out *o, const char *buf, size_t n);
    int count;  // total formatted length (untruncated) — the return value
    char *buf;  // buffer sink: destination; stream sink: flush chunk
    size_t cap; // buffer sink: capacity
    size_t pos; // buffer sink: bytes stored; stream sink: chunk fill
};

int cara_vformat(struct cara_fmt_out *o, const char *fmt, va_list ap);

#endif // CARA_LIBC_FMT_H
