// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara_libm (src/userland/libc/math.c, T.4.3). The
// implementations are pure double arithmetic, so math.c compiles straight
// into this host test. Each transcendental is checked against a hardcoded
// high-precision reference value within a relative tolerance, plus a few
// domain edges (NaN / -inf). These are the functions the amiCalc port links.

#include <stdio.h>

// Reference cara_libm's <math.h> by path so the libc include dir (which has a
// freestanding stdio.h) does not shadow the host's standard headers.
#include "../../src/userland/libc/include/math.h"

static int g_fails;

static double ad(double a, double b)
{
    double d = a - b;
    return d < 0 ? -d : d;
}

static void chk(const char *name, double got, double want)
{
    double tol = (ad(want, 0.0) + 1.0) * 1e-11;
    if (ad(got, want) > tol) {
        fprintf(stderr, "test_libc_math: FAIL %s: got %.17g want %.17g\n", name, got, want);
        g_fails++;
    }
}

static void chk_nan(const char *name, double got)
{
    if (got == got) {
        fprintf(stderr, "test_libc_math: FAIL %s: got %.17g want NaN\n", name, got);
        g_fails++;
    }
}

int main(void)
{
    // sqrt
    chk("sqrt(2)", sqrt(2.0), 1.4142135623730951);
    chk("sqrt(16)", sqrt(16.0), 4.0);
    chk("sqrt(0.25)", sqrt(0.25), 0.5);
    chk("sqrt(1e6)", sqrt(1e6), 1000.0);
    chk("sqrt(0)", sqrt(0.0), 0.0);

    // exp
    chk("exp(0)", exp(0.0), 1.0);
    chk("exp(1)", exp(1.0), 2.718281828459045);
    chk("exp(-1)", exp(-1.0), 0.36787944117144233);
    chk("exp(5)", exp(5.0), 148.4131591025766);
    chk("exp(10)", exp(10.0), 22026.465794806718);

    // log
    chk("log(1)", log(1.0), 0.0);
    chk("log(e)", log(M_E), 1.0);
    chk("log(10)", log(10.0), 2.302585092994046);
    chk("log(0.5)", log(0.5), -0.6931471805599453);
    chk("log(1e6)", log(1e6), 13.815510557964274);

    // sin / cos / tan
    chk("sin(0)", sin(0.0), 0.0);
    chk("sin(1)", sin(1.0), 0.8414709848078965);
    chk("sin(pi/6)", sin(M_PI / 6.0), 0.5);
    chk("sin(10)", sin(10.0), -0.5440211108893698);
    chk("sin(-2)", sin(-2.0), -0.9092974268256817);
    chk("cos(0)", cos(0.0), 1.0);
    chk("cos(1)", cos(1.0), 0.5403023058681398);
    chk("cos(pi/3)", cos(M_PI / 3.0), 0.5);
    chk("cos(10)", cos(10.0), -0.8390715290764524);
    chk("tan(1)", tan(1.0), 1.5574077246549023);
    chk("tan(0.5)", tan(0.5), 0.5463024898437905);

    // atan / asin / acos
    chk("atan(1)", atan(1.0), 0.7853981633974483);
    chk("atan(1000)", atan(1000.0), 1.5697963271282298);
    chk("atan(-0.5)", atan(-0.5), -0.4636476090008061);
    chk("asin(0.5)", asin(0.5), 0.5235987755982989);
    chk("asin(1)", asin(1.0), 1.5707963267948966);
    chk("asin(-1)", asin(-1.0), -1.5707963267948966);
    chk("acos(0.5)", acos(0.5), 1.0471975511965976);
    chk("acos(1)", acos(1.0), 0.0);
    chk("acos(0)", acos(0.0), 1.5707963267948966);

    // pow
    chk("pow(2,10)", pow(2.0, 10.0), 1024.0);
    chk("pow(2,-3)", pow(2.0, -3.0), 0.125);
    chk("pow(9,0.5)", pow(9.0, 0.5), 3.0);
    chk("pow(2,0.5)", pow(2.0, 0.5), 1.4142135623730951);
    chk("pow(10,3)", pow(10.0, 3.0), 1000.0);
    chk("pow(2.5,2)", pow(2.5, 2.0), 6.25);
    chk("pow(-2,3)", pow(-2.0, 3.0), -8.0);
    chk("pow(x,0)", pow(123.0, 0.0), 1.0);

    // fabs
    chk("fabs(-3.5)", fabs(-3.5), 3.5);
    chk("fabs(2)", fabs(2.0), 2.0);

    // domain edges
    chk_nan("sqrt(-1)", sqrt(-1.0));
    chk_nan("asin(2)", asin(2.0));
    chk_nan("log(-1)", log(-1.0));
    if (!(log(0.0) < -1e300)) {
        fprintf(stderr, "test_libc_math: FAIL log(0): want -inf\n");
        g_fails++;
    }

    if (g_fails) {
        fprintf(stderr, "test_libc_math: %d failure(s)\n", g_fails);
        return 1;
    }
    printf("test_libc_math: ok\n");
    return 0;
}
