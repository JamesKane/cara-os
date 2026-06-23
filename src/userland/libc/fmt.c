// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS userland libc — the printf formatting core (Phase T.1). A single
// vformat() drives both the buffer sinks (s(n)printf) and the stream sink
// (printf/fprintf, in stdio.c). Supports the integer/string/char/pointer
// conversions with flags (-, 0, +, space, #), width/precision (incl. *),
// and length modifiers (h/hh/l/ll/z/j/t). Floating point is deferred
// (docs/PORTS.md §5) — a %f/%g/%e directive emits a literal "%f"-style
// token so the call still completes. docs/PORTS.md §1.1.

#include <stdarg.h>
#include <stddef.h>

#include "fmt.h"

static void emit(struct cara_fmt_out *o, const char *s, size_t n)
{
    o->write(o, s, n);
    o->count += n;
}

static void emit_pad(struct cara_fmt_out *o, char c, int n)
{
    char buf[16];
    for (int i = 0; i < 16; i++) {
        buf[i] = c;
    }
    while (n > 0) {
        int chunk = n < 16 ? n : 16;
        emit(o, buf, (size_t)chunk);
        n -= chunk;
    }
}

// Flags.
#define F_LEFT 0x01
#define F_ZERO 0x02
#define F_PLUS 0x04
#define F_SPACE 0x08
#define F_ALT 0x10

static void fmt_uint(struct cara_fmt_out *o, unsigned long long v, int base, bool upper, int neg,
                     int flags, int width, int prec)
{
    char digits[24];
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (v == 0) {
        digits[n++] = '0';
    }
    while (v) {
        digits[n++] = set[v % (unsigned)base];
        v /= (unsigned)base;
    }
    // Precision = minimum number of digits.
    while (prec >= 0 && n < prec) {
        digits[n++] = '0';
    }

    char sign = 0;
    if (neg) {
        sign = '-';
    } else if (flags & F_PLUS) {
        sign = '+';
    } else if (flags & F_SPACE) {
        sign = ' ';
    }

    char prefix[2];
    int plen = 0;
    if ((flags & F_ALT) && base == 16 && n > 0) {
        prefix[0] = '0';
        prefix[1] = upper ? 'X' : 'x';
        plen = 2;
    }

    int body = n + (sign ? 1 : 0) + plen;
    int pad = width - body;

    // zero-pad only when right-justified and no explicit precision.
    bool zero = (flags & F_ZERO) && !(flags & F_LEFT) && prec < 0;

    if (!(flags & F_LEFT) && !zero && pad > 0) {
        emit_pad(o, ' ', pad);
    }
    if (sign) {
        emit(o, &sign, 1);
    }
    if (plen) {
        emit(o, prefix, (size_t)plen);
    }
    if (zero && pad > 0) {
        emit_pad(o, '0', pad);
    }
    for (int i = n; i-- > 0;) {
        emit(o, &digits[i], 1);
    }
    if ((flags & F_LEFT) && pad > 0) {
        emit_pad(o, ' ', pad);
    }
}

static void fmt_str(struct cara_fmt_out *o, const char *s, int flags, int width, int prec)
{
    if (!s) {
        s = "(null)";
    }
    int len = 0;
    while (s[len] && (prec < 0 || len < prec)) {
        len++;
    }
    int pad = width - len;
    if (!(flags & F_LEFT) && pad > 0) {
        emit_pad(o, ' ', pad);
    }
    emit(o, s, (size_t)len);
    if ((flags & F_LEFT) && pad > 0) {
        emit_pad(o, ' ', pad);
    }
}

int cara_vformat(struct cara_fmt_out *o, const char *fmt, va_list ap)
{
    o->count = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            emit(o, p, 1);
            continue;
        }
        p++;
        if (*p == '%') {
            emit(o, p, 1);
            continue;
        }
        // Flags.
        int flags = 0;
        for (;; p++) {
            if (*p == '-') {
                flags |= F_LEFT;
            } else if (*p == '0') {
                flags |= F_ZERO;
            } else if (*p == '+') {
                flags |= F_PLUS;
            } else if (*p == ' ') {
                flags |= F_SPACE;
            } else if (*p == '#') {
                flags |= F_ALT;
            } else {
                break;
            }
        }
        // Width.
        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= F_LEFT;
                width = -width;
            }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p++ - '0');
            }
        }
        // Precision.
        int prec = -1;
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') {
                prec = va_arg(ap, int);
                p++;
            } else {
                while (*p >= '0' && *p <= '9') {
                    prec = prec * 10 + (*p++ - '0');
                }
            }
            if (prec < 0) {
                prec = -1;
            }
        }
        // Length modifiers.
        int lng = 0; // 0=int, 1=long, 2=long long, 3=size_t
        if (*p == 'h') {
            p++;
            if (*p == 'h') {
                p++;
            }
        } else if (*p == 'l') {
            p++;
            lng = 1;
            if (*p == 'l') {
                p++;
                lng = 2;
            }
        } else if (*p == 'z' || *p == 'j' || *p == 't') {
            p++;
            lng = 3;
        }

        switch (*p) {
        case 'd':
        case 'i': {
            long long v;
            if (lng == 2) {
                v = va_arg(ap, long long);
            } else if (lng == 1) {
                v = va_arg(ap, long);
            } else if (lng == 3) {
                v = (long long)va_arg(ap, size_t);
            } else {
                v = va_arg(ap, int);
            }
            int neg = v < 0;
            unsigned long long uv = neg ? (unsigned long long)(-v) : (unsigned long long)v;
            fmt_uint(o, uv, 10, false, neg, flags, width, prec);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long long v;
            if (lng == 2) {
                v = va_arg(ap, unsigned long long);
            } else if (lng == 1) {
                v = va_arg(ap, unsigned long);
            } else if (lng == 3) {
                v = (unsigned long long)va_arg(ap, size_t);
            } else {
                v = va_arg(ap, unsigned int);
            }
            int base = (*p == 'o') ? 8 : (*p == 'u') ? 10 : 16;
            fmt_uint(o, v, base, *p == 'X', 0, flags, width, prec);
            break;
        }
        case 'p': {
            void *ptr = va_arg(ap, void *);
            emit(o, "0x", 2);
            fmt_uint(o, (unsigned long long)(size_t)ptr, 16, false, 0, flags, 0, -1);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            int pad = width - 1;
            if (!(flags & F_LEFT) && pad > 0) {
                emit_pad(o, ' ', pad);
            }
            emit(o, &c, 1);
            if ((flags & F_LEFT) && pad > 0) {
                emit_pad(o, ' ', pad);
            }
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            fmt_str(o, s, flags, width, prec);
            break;
        }
        case 'f':
        case 'F':
        case 'g':
        case 'G':
        case 'e':
        case 'E': {
            // Floating point deferred (docs/PORTS.md §5): consume the arg,
            // emit a placeholder so the call still completes.
            (void)va_arg(ap, double);
            char tok[2] = { '%', *p };
            emit(o, tok, 2);
            break;
        }
        case 0:
            return o->count; // trailing '%'
        default:
            emit(o, p, 1); // unknown — emit verbatim
            break;
        }
    }
    return o->count;
}
