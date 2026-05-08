// SPDX-License-Identifier: BSD-2-Clause
//
// Freestanding mem* helpers. The C standard requires these in the
// freestanding subset; the compiler emits calls to them for struct
// assignment and aggregate zero-init. Tiny byte-loop versions are
// fine for boot-path use; later we'll specialise on alignment if
// any hot path needs it.

#include <cara/types.h>

void *memcpy(void *dst, const void *src, size_t n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    u8 *d = dst;
    const u8 *s = src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    u8 *d = dst;
    while (n--) {
        *d++ = (u8)c;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *p = a;
    const u8 *q = b;
    while (n--) {
        if (*p != *q) {
            return (int)*p - (int)*q;
        }
        p++;
        q++;
    }
    return 0;
}
