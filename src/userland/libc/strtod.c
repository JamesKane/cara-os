// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS userland libc — strtod / atof (Phase T.4.2, for the amiCalc port).
// Parses [ws][sign]digits[.digits][(e|E)[sign]digits] and scales the
// significand by 10^exp via binary exponentiation. Self-contained (its own
// char classifiers, no syscalls) so it compiles for both riscv64 userland
// and the host unit test. Not a correctly-rounded parser at the extremes,
// but accurate for the calculator's inputs. docs/PORTS.md §3 (T.4.2).

#include <stddef.h>
#include <stdint.h>

double strtod(const char *s, char **endptr);
double atof(const char *s);

static int sd_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int sd_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

double strtod(const char *s, char **endptr)
{
    const char *p = s;
    while (sd_isspace((unsigned char)*p)) {
        p++;
    }

    int neg = 0;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }

    double mant = 0.0;
    int any = 0;  // any significand digit seen
    int fdig = 0; // fractional digit count
    int seendot = 0;
    for (;; p++) {
        if (sd_isdigit((unsigned char)*p)) {
            mant = mant * 10.0 + (double)(*p - '0');
            if (seendot) {
                fdig++;
            }
            any = 1;
        } else if (*p == '.' && !seendot) {
            seendot = 1;
        } else {
            break;
        }
    }

    int exp = 0;
    if (any && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        int es = 0;
        if (*q == '+' || *q == '-') {
            es = (*q == '-');
            q++;
        }
        if (sd_isdigit((unsigned char)*q)) {
            int ev = 0;
            while (sd_isdigit((unsigned char)*q)) {
                ev = ev * 10 + (*q - '0');
                q++;
            }
            exp = es ? -ev : ev;
            p = q;
        }
    }

    int totexp = exp - fdig;
    double scale = 1.0;
    double base = 10.0;
    int n = totexp < 0 ? -totexp : totexp;
    while (n) {
        if (n & 1) {
            scale *= base;
        }
        base *= base;
        n >>= 1;
    }
    double val = (totexp < 0) ? mant / scale : mant * scale;

    if (endptr) {
        // Cast through uintptr_t so -Wcast-qual doesn't flag the const drop
        // that strtod's char** API inherently requires.
        *endptr = (char *)(uintptr_t)(any ? p : s);
    }
    return neg ? -val : val;
}

double atof(const char *s)
{
    return strtod(s, NULL);
}
