// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for the CaraOS userland libc floating-point support
// added in T.4.2: the %f/%e/%g formatting in fmt.c and strtod in strtod.c
// (both pure, no syscalls, so they compile straight into this host test).
// Exercises the conversions amiCalc relies on (%.15g + strtod) plus flags,
// width, precision, special values, and a format→parse round-trip.

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "fmt.h" // src/userland/libc (added to the include path by CMake)

double strtod(const char *s, char **endptr); // from strtod.c

static int g_fails;

// Buffer sink for cara_vformat.
static void buf_write(struct cara_fmt_out *o, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (o->pos + 1 < o->cap) {
            o->buf[o->pos++] = s[i];
        }
    }
}

static int testfmt(char *out, size_t cap, const char *f, ...)
{
    struct cara_fmt_out o = { .write = buf_write, .count = 0, .buf = out, .cap = cap, .pos = 0 };
    va_list ap;
    va_start(ap, f);
    int r = cara_vformat(&o, f, ap);
    va_end(ap);
    out[o.pos] = 0;
    return r;
}

#define EXPECT(expected, fmt, ...)                                                                 \
    do {                                                                                           \
        char _b[128];                                                                              \
        testfmt(_b, sizeof(_b), (fmt), __VA_ARGS__);                                               \
        if (strcmp(_b, (expected)) != 0) {                                                         \
            fprintf(stderr, "test_libc_float: FAIL: \"%s\" → \"%s\" (want \"%s\")\n", (fmt), _b,   \
                    (expected));                                                                   \
            g_fails++;                                                                             \
        }                                                                                          \
    } while (0)

static void check_roundtrip(double v)
{
    char b[64];
    testfmt(b, sizeof(b), "%.17g", v);
    double back = strtod(b, NULL);
    double tol = fabs(v) * 1e-12 + 1e-300;
    if (fabs(back - v) > tol) {
        fprintf(stderr, "test_libc_float: FAIL roundtrip: %.17g → \"%s\" → %.17g\n", v, b, back);
        g_fails++;
    }
}

int main(void)
{
    // ---- %g (the amiCalc consumer: %.15g) ----
    EXPECT("4", "%.15g", 4.0);
    EXPECT("0.5", "%.15g", 0.5);
    EXPECT("-2.5", "%.15g", -2.5);
    EXPECT("100", "%.15g", 100.0);
    EXPECT("1000000", "%.15g", 1000000.0);
    EXPECT("1.5", "%.15g", 1.5);
    EXPECT("0", "%.15g", 0.0);
    EXPECT("3.14159", "%.15g", 3.14159);
    EXPECT("1e+20", "%.15g", 1e20);
    EXPECT("1e-07", "%.15g", 1e-7);
    // default precision (6 significant digits)
    EXPECT("0.0001", "%g", 0.0001);
    EXPECT("100000", "%g", 100000.0);
    EXPECT("1e+06", "%g", 1000000.0);

    // ---- %f ----
    EXPECT("3.14", "%.2f", 3.14159);
    EXPECT("1.500000", "%f", 1.5);
    EXPECT("7", "%.0f", 7.0);
    EXPECT("0.500", "%.3f", 0.5);
    EXPECT("-2.50", "%.2f", -2.5);

    // ---- %e ----
    EXPECT("0.000000e+00", "%e", 0.0);
    EXPECT("3.14e+04", "%.2e", 31400.0);
    EXPECT("1.000e+00", "%.3e", 1.0);

    // ---- flags / width ----
    EXPECT("+2.0", "%+.1f", 2.0);
    EXPECT("00003.50", "%08.2f", 3.5);
    EXPECT("   1.5", "%6.1f", 1.5);

    // ---- special values ----
    EXPECT("inf", "%f", (double)INFINITY);
    EXPECT("-inf", "%f", (double)-INFINITY);
    EXPECT("nan", "%f", (double)NAN);

    // ---- strtod ----
    if (fabs(strtod("3.14", NULL) - 3.14) > 1e-12) {
        fprintf(stderr, "test_libc_float: FAIL strtod(3.14)\n");
        g_fails++;
    }
    if (strtod("-2.5e3", NULL) != -2500.0) {
        fprintf(stderr, "test_libc_float: FAIL strtod(-2.5e3)\n");
        g_fails++;
    }
    if (fabs(strtod(".5", NULL) - 0.5) > 1e-12) {
        fprintf(stderr, "test_libc_float: FAIL strtod(.5)\n");
        g_fails++;
    }
    if (strtod("1e3", NULL) != 1000.0) {
        fprintf(stderr, "test_libc_float: FAIL strtod(1e3)\n");
        g_fails++;
    }
    {
        char *end = NULL;
        double v = strtod("  42abc", &end);
        if (v != 42.0 || end == NULL || *end != 'a') {
            fprintf(stderr, "test_libc_float: FAIL strtod endptr\n");
            g_fails++;
        }
    }

    // ---- format → parse round-trip ----
    check_roundtrip(0.1);
    check_roundtrip(3.14159265358979);
    check_roundtrip(2.718281828459045);
    check_roundtrip(1234.5678);
    check_roundtrip(-9876.54321);
    check_roundtrip(1e-10);
    check_roundtrip(1e15);

    if (g_fails) {
        fprintf(stderr, "test_libc_float: %d failure(s)\n", g_fails);
        return 1;
    }
    printf("test_libc_float: ok\n");
    return 0;
}
