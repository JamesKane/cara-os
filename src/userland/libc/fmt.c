// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS userland libc — the printf formatting core (Phase T.1). A single
// vformat() drives both the buffer sinks (s(n)printf) and the stream sink
// (printf/fprintf, in stdio.c). Supports the integer/string/char/pointer
// conversions with flags (-, 0, +, space, #), width/precision (incl. *),
// and length modifiers (h/hh/l/ll/z/j/t). Floating point (%f/%F/%e/%E/%g/%G)
// is a no-libm dtoa added in T.4.2 (fmt_float below) for the amiCalc port.
// docs/PORTS.md §1.1, §3 (T.4.2).

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

// ---- floating point (T.4.2) -----------------------------------------
// A pragmatic, no-libm dtoa for amiCalc's %.15g (and %f/%e). Bit-decode
// the double, scale its magnitude into [1,10) to find the decimal
// exponent, extract significant digits, and round on a guard. Good for
// display + round-trip at typical magnitudes; not a bit-exact Ryu/dragon4
// at the extremes of the exponent range (the scaling accumulates a little
// error). docs/PORTS.md §3 (T.4.2).

#define FMT_MAXSIG 18

// One significant digit at index i (zero past the generated/kept run).
static char fmt_sg(const char *sig, int i)
{
    return (i >= 0 && i < FMT_MAXSIG) ? sig[i] : '0';
}

// Generate FMT_MAXSIG significant digits of a (a >= 0, finite) into
// sig[0..FMT_MAXSIG-1], returning decimal exponent e: sig[0] has place 10^e.
static int fmt_gen(double a, char *sig)
{
    int e = 0;
    if (a > 0.0) {
        while (a >= 1e16) {
            a /= 1e16;
            e += 16;
        }
        while (a >= 10.0) {
            a /= 10.0;
            e += 1;
        }
        while (a < 1e-15) {
            a *= 1e16;
            e -= 16;
        }
        while (a < 1.0) {
            a *= 10.0;
            e -= 1;
        }
    }
    // Extract FMT_MAXSIG digits plus one guard, then round.
    char tmp[FMT_MAXSIG + 1];
    for (int i = 0; i <= FMT_MAXSIG; i++) {
        int dg = (int)a;
        if (dg < 0) {
            dg = 0;
        }
        if (dg > 9) {
            dg = 9;
        }
        tmp[i] = (char)('0' + dg);
        a = (a - (double)dg) * 10.0;
    }
    if (tmp[FMT_MAXSIG] >= '5') {
        int k = FMT_MAXSIG - 1;
        for (; k >= 0; k--) {
            if (tmp[k] == '9') {
                tmp[k] = '0';
            } else {
                tmp[k]++;
                break;
            }
        }
        if (k < 0) {
            for (int j = FMT_MAXSIG - 1; j > 0; j--) {
                tmp[j] = tmp[j - 1];
            }
            tmp[0] = '1';
            e += 1;
        }
    }
    for (int i = 0; i < FMT_MAXSIG; i++) {
        sig[i] = tmp[i];
    }
    return e;
}

// Round sig[] to `keep` significant digits (1..FMT_MAXSIG); zero the rest.
// On carry-out (999..→1000..) shift and bump *ep.
static void fmt_round(char *sig, int *ep, int keep)
{
    if (keep < 1) {
        keep = 1;
    }
    if (keep >= FMT_MAXSIG) {
        return;
    }
    if (sig[keep] >= '5') {
        int k = keep - 1;
        for (; k >= 0; k--) {
            if (sig[k] == '9') {
                sig[k] = '0';
            } else {
                sig[k]++;
                break;
            }
        }
        if (k < 0) {
            for (int j = keep - 1; j > 0; j--) {
                sig[j] = sig[j - 1];
            }
            sig[0] = '1';
            *ep += 1;
        }
    }
    for (int i = keep; i < FMT_MAXSIG; i++) {
        sig[i] = '0';
    }
}

// Build a fixed-notation body (no sign) into b: integer part then `fp`
// fraction digits. e = place of sig[0].
static int fmt_body_f(char *b, const char *sig, int e, int fp, int flags)
{
    int n = 0;
    if (e >= 0) {
        for (int i = 0; i <= e; i++) {
            b[n++] = fmt_sg(sig, i);
        }
    } else {
        b[n++] = '0';
    }
    if (fp > 0 || (flags & F_ALT)) {
        b[n++] = '.';
        if (e >= 0) {
            for (int j = 0; j < fp; j++) {
                b[n++] = fmt_sg(sig, e + 1 + j);
            }
        } else {
            int lead = -e - 1;
            for (int j = 0; j < fp; j++) {
                b[n++] = (j < lead) ? '0' : fmt_sg(sig, j - lead);
            }
        }
    }
    return n;
}

// Build a scientific body (no sign): d.dddde±NN with `fp` fraction digits.
static int fmt_body_e(char *b, const char *sig, int e, int fp, bool upper, int flags)
{
    int n = 0;
    b[n++] = fmt_sg(sig, 0);
    if (fp > 0 || (flags & F_ALT)) {
        b[n++] = '.';
        for (int i = 1; i <= fp; i++) {
            b[n++] = fmt_sg(sig, i);
        }
    }
    b[n++] = upper ? 'E' : 'e';
    int ev = e;
    if (ev < 0) {
        b[n++] = '-';
        ev = -ev;
    } else {
        b[n++] = '+';
    }
    char eb[8];
    int en = 0;
    if (ev == 0) {
        eb[en++] = '0';
    }
    while (ev) {
        eb[en++] = (char)('0' + ev % 10);
        ev /= 10;
    }
    while (en < 2) {
        eb[en++] = '0';
    }
    while (en > 0) {
        b[n++] = eb[--en];
    }
    return n;
}

// Strip trailing fraction zeros (and a bare '.') from the mantissa of body,
// preserving any exponent suffix. For %g without the # flag.
static int fmt_strip_zeros(char *b, int n)
{
    int epos = -1;
    for (int i = 0; i < n; i++) {
        if (b[i] == 'e' || b[i] == 'E') {
            epos = i;
            break;
        }
    }
    int mant_end = (epos < 0) ? n : epos;
    int dot = -1;
    for (int i = 0; i < mant_end; i++) {
        if (b[i] == '.') {
            dot = i;
            break;
        }
    }
    if (dot < 0) {
        return n;
    }
    int last = mant_end - 1;
    while (last > dot && b[last] == '0') {
        last--;
    }
    if (last == dot) {
        last--; // drop the '.' too
    }
    int keep = last + 1;
    if (epos >= 0) {
        for (int i = epos; i < n; i++) {
            b[keep++] = b[i];
        }
    }
    return keep;
}

// Emit a numeric body with optional sign and width/zero padding.
static void fmt_field(struct cara_fmt_out *o, char sign, const char *b, int bn, int flags,
                      int width)
{
    int total = bn + (sign ? 1 : 0);
    int pad = width - total;
    bool zero = (flags & F_ZERO) && !(flags & F_LEFT);
    if (!(flags & F_LEFT) && !zero && pad > 0) {
        emit_pad(o, ' ', pad);
    }
    if (sign) {
        emit(o, &sign, 1);
    }
    if (zero && pad > 0) {
        emit_pad(o, '0', pad);
    }
    emit(o, b, (size_t)bn);
    if ((flags & F_LEFT) && pad > 0) {
        emit_pad(o, ' ', pad);
    }
}

static void fmt_float(struct cara_fmt_out *o, double v, char conv, int flags, int width, int prec)
{
    bool upper = (conv == 'F' || conv == 'E' || conv == 'G');
    char lc = upper ? (char)(conv + 32) : conv;

    union {
        double d;
        unsigned long long u;
    } bits;
    bits.d = v;
    int neg = (int)((bits.u >> 63) & 1u);
    int bexp = (int)((bits.u >> 52) & 0x7ffu);
    unsigned long long mantbits = bits.u & 0xfffffffffffffULL;

    char sign = neg ? '-' : (flags & F_PLUS) ? '+' : (flags & F_SPACE) ? ' ' : 0;

    if (bexp == 0x7ff) { // inf / nan
        const char *s = mantbits ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        char b[4];
        int n = 0;
        while (s[n]) {
            b[n] = s[n];
            n++;
        }
        fmt_field(o, sign, b, n, flags & ~F_ZERO, width);
        return;
    }

    if (prec < 0) {
        prec = 6;
    }
    double a = neg ? -v : v;
    char sig[FMT_MAXSIG];
    int e = fmt_gen(a, sig);

    char body[80];
    int bn;
    if (lc == 'e') {
        fmt_round(sig, &e, prec + 1);
        bn = fmt_body_e(body, sig, e, prec, upper, flags);
    } else if (lc == 'f') {
        int keep = e + prec + 1;
        fmt_round(sig, &e, keep < 1 ? 1 : keep);
        bn = fmt_body_f(body, sig, e, prec, flags);
    } else { // g / G
        int P = prec == 0 ? 1 : prec;
        if (P > FMT_MAXSIG) {
            P = FMT_MAXSIG;
        }
        fmt_round(sig, &e, P);
        int X = e;
        if (X >= -4 && X < P) {
            bn = fmt_body_f(body, sig, e, P - 1 - X, flags);
        } else {
            bn = fmt_body_e(body, sig, e, P - 1, upper, flags);
        }
        if (!(flags & F_ALT)) {
            bn = fmt_strip_zeros(body, bn);
        }
    }
    fmt_field(o, sign, body, bn, flags, width);
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
            double dv = va_arg(ap, double);
            fmt_float(o, dv, *p, flags, width, prec);
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
