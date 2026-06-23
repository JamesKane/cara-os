// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS userland libc — <stdio.h> formatted output (Phase T.1). Two sinks
// over the shared printf core (fmt.c): a buffer sink for s(n)printf, and a
// line-buffered console sink (printf/fprintf/puts/putchar/…) over
// SYS_LOG_WRITE — output appears on the QEMU console/log. stdout/stderr are
// sentinels; both render to the console in v0 (a real per-stream / disk
// FILE arrives when a port needs it, T.2). docs/PORTS.md §1.1.

#include <cara/sysno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include "fmt.h"

struct _CARA_FILE {
    int fd; // 1 = stdout, 2 = stderr
};
static FILE g_stdout = { 1 };
static FILE g_stderr = { 2 };
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

// ---- s(n)printf buffer sink -----------------------------------------

static void buf_write(struct cara_fmt_out *o, const char *s, size_t n)
{
    if (o->cap == 0) {
        return;
    }
    size_t limit = o->cap - 1; // reserve the terminating NUL
    if (o->pos >= limit) {
        return;
    }
    size_t room = limit - o->pos;
    size_t k = n < room ? n : room;
    for (size_t i = 0; i < k; i++) {
        o->buf[o->pos + i] = s[i];
    }
    o->pos += k;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
    struct cara_fmt_out o = { .write = buf_write, .count = 0, .buf = str, .cap = size, .pos = 0 };
    int total = cara_vformat(&o, fmt, ap);
    if (size > 0) {
        size_t term = o.pos < size ? o.pos : size - 1;
        str[term] = 0;
    }
    return total;
}

int snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *str, const char *fmt, va_list ap)
{
    return vsnprintf(str, (size_t)-1, fmt, ap);
}

int sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(str, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}

// ---- line-buffered console sink (over SYS_LOG_WRITE) ------------------

static char g_line[512];
static size_t g_linen;

static void log_emit(const char *s, size_t n)
{
    // SYS_LOG_WRITE(level, tag, msg, len). Chunk well under the kernel's
    // per-record message cap.
    while (n > 0) {
        size_t chunk = n < 200 ? n : 200;
        register long a0 __asm__("a0") = 2;           // INFO
        register long a1 __asm__("a1") = (long)"app"; // tag
        register long a2 __asm__("a2") = (long)s;     // msg
        register long a3 __asm__("a3") = (long)chunk; // len
        register long a7 __asm__("a7") = SYS_LOG_WRITE;
        __asm__ volatile("ecall" ::"r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
        s += chunk;
        n -= chunk;
    }
}

static void line_flush(void)
{
    if (g_linen) {
        log_emit(g_line, g_linen); // the kernel log adds the line break
        g_linen = 0;
    }
}

// Append bytes; flush a completed line on '\n' (the newline itself is
// dropped — the log record is one line) or when the buffer fills.
static void console_put(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n') {
            line_flush();
            continue;
        }
        if (g_linen >= sizeof g_line - 1) {
            line_flush();
        }
        g_line[g_linen++] = c;
    }
}

int vfprintf(FILE *stream, const char *fmt, va_list ap)
{
    (void)stream; // stdout/stderr both render to the console in v0
    char buf[512];
    int total = vsnprintf(buf, sizeof buf, fmt, ap);
    int emit = total < (int)sizeof buf - 1 ? total : (int)sizeof buf - 1;
    console_put(buf, (size_t)emit);
    return total;
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int fputs(const char *s, FILE *stream)
{
    (void)stream;
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    console_put(s, n);
    return 0;
}

int puts(const char *s)
{
    fputs(s, stdout);
    console_put("\n", 1);
    return 0;
}

int fputc(int c, FILE *stream)
{
    (void)stream;
    char ch = (char)c;
    console_put(&ch, 1);
    return (unsigned char)c;
}

int putc(int c, FILE *stream)
{
    return fputc(c, stream);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    (void)stream;
    size_t total = size * nmemb;
    console_put((const char *)ptr, total);
    return size ? nmemb : 0;
}

int fflush(FILE *stream)
{
    (void)stream;
    line_flush();
    return 0;
}
