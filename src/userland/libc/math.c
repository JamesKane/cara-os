// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS userland libm (cara_libm, Phase T.4.3). Own implementations of the
// transcendentals the amiCalc port references — no third-party libm linked
// (docs/PRINCIPLES.md §2; reading musl/openlibm to cross-check in tests is
// fine, linking is not). Everything is portable C double arithmetic (which
// is hardware FP on RV64 once the FPU is on, T.4.1, and native on the host
// unit test), so the same source compiles for both. Accuracy is "good for a
// calculator" — range reduction + series/Newton, not a bit-exact libm.
//
// Reductions: sqrt via Newton from a bit-hack guess; exp/log via 2^k / m·2^e
// decomposition + a small series on the reduced argument; sin/cos via
// quadrant reduction + Taylor; atan via 1/x and π/8 reductions + series;
// asin/acos built from atan + sqrt; pow via integer-exponent squaring (exact
// for small powers) else exp(y·log x). docs/PORTS.md §3 (T.4.3).

#include <stddef.h>

#include "include/math.h"

#define CARA_PIO2 1.57079632679489661923
#define CARA_PIO4 0.78539816339744830961
#define CARA_2_PI 0.63661977236758134308
#define CARA_LN2 0.69314718055994530942
#define CARA_LOG2E 1.44269504088896340736
#define CARA_SQRT2 1.41421356237309504880
#define CARA_TAN_PIO8 0.41421356237309504880

// ---- bit helpers ----------------------------------------------------

static double m_nan(void)
{
    union {
        unsigned long long u;
        double d;
    } v;
    v.u = 0x7ff8000000000000ULL;
    return v.d;
}

static double m_inf(int neg)
{
    union {
        unsigned long long u;
        double d;
    } v;
    v.u = neg ? 0xfff0000000000000ULL : 0x7ff0000000000000ULL;
    return v.d;
}

static int m_isnan(double x)
{
    return x != x;
}

static int m_isinf(double x)
{
    union {
        double d;
        unsigned long long u;
    } v;
    v.d = x;
    return ((v.u >> 52) & 0x7ffULL) == 0x7ff && (v.u & 0xfffffffffffffULL) == 0;
}

double fabs(double x)
{
    union {
        double d;
        unsigned long long u;
    } v;
    v.d = x;
    v.u &= 0x7fffffffffffffffULL;
    return v.d;
}

// m * 2^k, ignoring denormals at the extreme underflow tail.
static double m_ldexp(double m, int k)
{
    if (k > 1023) {
        return m * 8.98846567431158e307 * 8.98846567431158e307; // → ±inf
    }
    if (k < -1022) {
        m *= 2.2250738585072014e-308; // 2^-1022
        k += 1022;
        if (k < -1022) {
            return 0.0 * m; // sign-preserving 0
        }
    }
    union {
        unsigned long long u;
        double d;
    } v;
    v.u = (unsigned long long)(k + 1023) << 52;
    return m * v.d;
}

// ---- sqrt -----------------------------------------------------------

double sqrt(double x)
{
    if (m_isnan(x)) {
        return x;
    }
    if (x < 0.0) {
        return m_nan();
    }
    if (x == 0.0 || m_isinf(x)) {
        return x;
    }
    union {
        double d;
        unsigned long long u;
    } v;
    v.d = x;
    v.u = (v.u + (1023ULL << 52)) >> 1; // exponent-halving initial guess
    double y = v.d;
    for (int i = 0; i < 5; i++) {
        y = 0.5 * (y + x / y); // Newton: quadratic convergence
    }
    return y;
}

// ---- exp / log ------------------------------------------------------

double exp(double x)
{
    if (m_isnan(x)) {
        return x;
    }
    if (x > 709.782712893384) {
        return m_inf(0);
    }
    if (x < -745.13321910194) {
        return 0.0;
    }
    double kf = x * CARA_LOG2E;
    int k = (int)(kf >= 0.0 ? kf + 0.5 : kf - 0.5);
    double r = x - (double)k * CARA_LN2; // |r| <= ln2/2
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 16; n++) {
        term *= r / (double)n;
        sum += term;
    }
    return m_ldexp(sum, k);
}

double log(double x)
{
    if (m_isnan(x)) {
        return x;
    }
    if (x < 0.0) {
        return m_nan();
    }
    if (x == 0.0) {
        return m_inf(1);
    }
    if (m_isinf(x)) {
        return x;
    }
    union {
        double d;
        unsigned long long u;
    } v;
    v.d = x;
    int e;
    if (((v.u >> 52) & 0x7ffULL) == 0) { // denormal: normalise first
        v.d = x * 4.503599627370496e15;  // 2^52
        e = (int)((v.u >> 52) & 0x7ffULL) - 1023 - 52;
    } else {
        e = (int)((v.u >> 52) & 0x7ffULL) - 1023;
    }
    v.u = (v.u & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL; // m in [1,2)
    double m = v.d;
    if (m > CARA_SQRT2) {
        m *= 0.5;
        e += 1;
    }
    // log(m) = 2·atanh(t), t = (m-1)/(m+1), |t| <= 0.172
    double t = (m - 1.0) / (m + 1.0);
    double t2 = t * t;
    double term = t;
    double sum = t;
    for (int n = 3; n <= 31; n += 2) {
        term *= t2;
        sum += term / (double)n;
    }
    return (double)e * CARA_LN2 + 2.0 * sum;
}

// ---- sin / cos / tan ------------------------------------------------

static double sin_core(double r) // |r| <= pi/4
{
    double r2 = r * r;
    double term = r;
    double sum = r;
    for (int n = 1; n <= 8; n++) {
        term *= -r2 / (double)((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

static double cos_core(double r) // |r| <= pi/4
{
    double r2 = r * r;
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n <= 8; n++) {
        term *= -r2 / (double)((2 * n - 1) * (2 * n));
        sum += term;
    }
    return sum;
}

double sin(double x)
{
    if (m_isnan(x)) {
        return x;
    }
    if (m_isinf(x)) {
        return m_nan();
    }
    double xf = x * CARA_2_PI;
    long n = (long)(xf >= 0.0 ? xf + 0.5 : xf - 0.5);
    double r = x - (double)n * CARA_PIO2;
    int q = (int)(n & 3);
    if (q < 0) {
        q += 4;
    }
    switch (q) {
    case 0:
        return sin_core(r);
    case 1:
        return cos_core(r);
    case 2:
        return -sin_core(r);
    default:
        return -cos_core(r);
    }
}

double cos(double x)
{
    if (m_isnan(x)) {
        return x;
    }
    if (m_isinf(x)) {
        return m_nan();
    }
    double xf = x * CARA_2_PI;
    long n = (long)(xf >= 0.0 ? xf + 0.5 : xf - 0.5);
    double r = x - (double)n * CARA_PIO2;
    int q = (int)(n & 3);
    if (q < 0) {
        q += 4;
    }
    switch (q) {
    case 0:
        return cos_core(r);
    case 1:
        return -sin_core(r);
    case 2:
        return -cos_core(r);
    default:
        return sin_core(r);
    }
}

double tan(double x)
{
    return sin(x) / cos(x);
}

// ---- atan / asin / acos ---------------------------------------------

double atan(double x)
{
    if (m_isnan(x)) {
        return x;
    }
    int neg = 0;
    if (x < 0.0) {
        neg = 1;
        x = -x;
    }
    if (m_isinf(x)) {
        return neg ? -CARA_PIO2 : CARA_PIO2;
    }
    int comp = 0;
    if (x > 1.0) { // atan(x) = pi/2 - atan(1/x)
        comp = 1;
        x = 1.0 / x;
    }
    double base = 0.0;
    if (x > CARA_TAN_PIO8) { // atan(x) = pi/4 + atan((x-1)/(x+1))
        base = CARA_PIO4;
        x = (x - 1.0) / (x + 1.0);
    }
    double t2 = x * x;
    double term = x;
    double sum = x;
    for (int n = 3; n <= 49; n += 2) {
        term *= -t2;
        sum += term / (double)n;
    }
    double r = base + sum;
    if (comp) {
        r = CARA_PIO2 - r;
    }
    return neg ? -r : r;
}

double asin(double x)
{
    if (m_isnan(x)) {
        return x;
    }
    if (x > 1.0 || x < -1.0) {
        return m_nan();
    }
    if (x == 1.0) {
        return CARA_PIO2;
    }
    if (x == -1.0) {
        return -CARA_PIO2;
    }
    return atan(x / sqrt(1.0 - x * x));
}

double acos(double x)
{
    if (m_isnan(x)) {
        return x;
    }
    if (x > 1.0 || x < -1.0) {
        return m_nan();
    }
    return CARA_PIO2 - asin(x);
}

// ---- pow ------------------------------------------------------------

double pow(double x, double y)
{
    if (y == 0.0) {
        return 1.0; // including pow(nan,0) == 1 per C
    }
    if (m_isnan(x) || m_isnan(y)) {
        return m_nan();
    }
    if (x == 1.0) {
        return 1.0;
    }
    // Integer exponent: exponentiation by squaring (exact for small powers,
    // and the only correct path for a negative base).
    if (y >= -1024.0 && y <= 1024.0 && y == (double)(long long)y) {
        long long e = (long long)y;
        int neg = e < 0;
        unsigned long long m = neg ? (unsigned long long)(-e) : (unsigned long long)e;
        double base = x;
        double acc = 1.0;
        while (m) {
            if (m & 1ULL) {
                acc *= base;
            }
            base *= base;
            m >>= 1;
        }
        return neg ? 1.0 / acc : acc;
    }
    if (x > 0.0) {
        return exp(y * log(x));
    }
    if (x == 0.0) {
        return (y > 0.0) ? 0.0 : m_inf(0);
    }
    return m_nan(); // negative base, non-integer exponent
}
