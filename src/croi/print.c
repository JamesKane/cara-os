// SPDX-License-Identifier: BSD-2-Clause
//
// Croi tiny printf. Supports %s %c %d %u %x %p %llu %llx; unknown
// conversions emit '?'. Width/precision/flags are not implemented; the
// caller pads strings themselves. CR is auto-prepended to LF so terminals
// stay aligned.

#include "print.h"

#include <stdarg.h>

#include <cara/arch.h>
#include <cara/types.h>

static Croi_PutcFn g_putc = arch_console_putc;

void Croi_PrintInstallBackend(Croi_PutcFn put)
{
    g_putc = put ? put : arch_console_putc;
}

void Croi_PutByte(char c)
{
    if (c == '\n') {
        g_putc('\r');
    }
    g_putc(c);
}

static void emit_str(const char *s)
{
    if (!s) {
        s = "(null)";
    }
    while (*s) {
        Croi_PutByte(*s++);
    }
}

static void emit_dec_u64(u64 v)
{
    char buf[24];
    int i = 0;
    if (v == 0) {
        Croi_PutByte('0');
        return;
    }
    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        Croi_PutByte(buf[--i]);
    }
}

static void emit_dec_i64(i64 v)
{
    if (v < 0) {
        Croi_PutByte('-');
        emit_dec_u64((u64)(-(v + 1)) + 1u);
        return;
    }
    emit_dec_u64((u64)v);
}

static void emit_hex_u64(u64 v, int min_digits)
{
    char buf[16];
    int i = 0;
    if (v == 0) {
        buf[i++] = '0';
    }
    while (v > 0) {
        u32 d = (u32)(v & 0xFu);
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v >>= 4;
    }
    while (i < min_digits) {
        buf[i++] = '0';
    }
    while (i > 0) {
        Croi_PutByte(buf[--i]);
    }
}

void Croi_Print(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        char c = *fmt++;
        if (c != '%') {
            Croi_PutByte(c);
            continue;
        }

        bool is_long_long = false;
        if (*fmt == 'l' && fmt[1] == 'l') {
            is_long_long = true;
            fmt += 2;
        } else if (*fmt == 'l') {
            is_long_long = true;
            fmt++;
        } else if (*fmt == 'z') {
            is_long_long = true;
            fmt++;
        }

        char conv = *fmt++;
        switch (conv) {
        case 's':
            emit_str(va_arg(ap, const char *));
            break;
        case 'c':
            Croi_PutByte((char)va_arg(ap, int));
            break;
        case 'd':
            if (is_long_long) {
                emit_dec_i64(va_arg(ap, i64));
            } else {
                emit_dec_i64((i64)va_arg(ap, int));
            }
            break;
        case 'u':
            if (is_long_long) {
                emit_dec_u64(va_arg(ap, u64));
            } else {
                emit_dec_u64((u64)va_arg(ap, unsigned int));
            }
            break;
        case 'x':
            if (is_long_long) {
                emit_hex_u64(va_arg(ap, u64), 0);
            } else {
                emit_hex_u64((u64)va_arg(ap, unsigned int), 0);
            }
            break;
        case 'p': {
            void *p = va_arg(ap, void *);
            Croi_PutByte('0');
            Croi_PutByte('x');
            emit_hex_u64((u64)(uptr)p, 16);
            break;
        }
        case '%':
            Croi_PutByte('%');
            break;
        case '\0':
            va_end(ap);
            return;
        default:
            Croi_PutByte('?');
            break;
        }
    }
    va_end(ap);
}
