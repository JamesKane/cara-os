// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS userland libc — <stdlib.h> (Phase T.1). malloc/free over exec
// AllocVec/FreeVec (an Amiga app's heap is AllocMem-backed anyway); integer
// conversion, qsort, rand, exit. docs/PORTS.md §1.1.

#include <cara/sysno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>

// ---- heap: malloc with a size header over AllocVec -------------------
// A 16-byte header keeps the returned pointer 16-aligned and records the
// payload size so free/realloc need no caller size.
#define MALLOC_HDR 16u

void *malloc(size_t size)
{
    if (size == 0) {
        size = 1;
    }
    unsigned char *raw = (unsigned char *)AllocVec((ULONG)(size + MALLOC_HDR), MEMF_ANY);
    if (!raw) {
        return nullptr;
    }
    *(size_t *)(void *)raw = size;
    return raw + MALLOC_HDR;
}

void free(void *ptr)
{
    if (ptr) {
        FreeVec((unsigned char *)ptr - MALLOC_HDR);
    }
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    if (nmemb && total / nmemb != size) {
        return nullptr; // overflow
    }
    void *p = malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return nullptr;
    }
    size_t old = *(size_t *)(void *)((unsigned char *)ptr - MALLOC_HDR);
    void *np = malloc(size);
    if (np) {
        memcpy(np, ptr, old < size ? old : size);
        free(ptr);
    }
    return np;
}

// ---- integer conversion ---------------------------------------------

long strtol(const char *s, char **endptr, int base)
{
    const char *p = s;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    int neg = 0;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    long val = 0;
    for (;; p++) {
        int c = (unsigned char)*p;
        int d;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            d = c - 'A' + 10;
        } else {
            break;
        }
        if (d >= base) {
            break;
        }
        val = val * base + d;
    }
    if (endptr) {
        *endptr = (char *)p;
    }
    return neg ? -val : val;
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
    return (unsigned long)strtol(s, endptr, base);
}

int atoi(const char *s)
{
    return (int)strtol(s, nullptr, 10);
}

long atol(const char *s)
{
    return strtol(s, nullptr, 10);
}

int abs(int n)
{
    return n < 0 ? -n : n;
}

long labs(long n)
{
    return n < 0 ? -n : n;
}

// ---- qsort (shellsort, byte-wise swap; no recursion / temp buffer) ---

static void qs_swap(unsigned char *a, unsigned char *b, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        unsigned char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *))
{
    unsigned char *a = (unsigned char *)base;
    for (size_t gap = nmemb / 2; gap > 0; gap /= 2) {
        for (size_t i = gap; i < nmemb; i++) {
            for (size_t j = i; j >= gap; j -= gap) {
                unsigned char *lo = a + (j - gap) * size;
                unsigned char *hi = a + j * size;
                if (cmp(lo, hi) <= 0) {
                    break;
                }
                qs_swap(lo, hi, size);
            }
        }
    }
}

// ---- rand (LCG) ------------------------------------------------------

static unsigned long g_rand_state = 1;

int rand(void)
{
    g_rand_state = g_rand_state * 1103515245ul + 12345ul;
    return (int)((g_rand_state >> 16) & 0x7fffffff);
}

void srand(unsigned seed)
{
    g_rand_state = seed;
}

// ---- exit / abort ----------------------------------------------------

static void libc_ecall_exit(long status)
{
    register long a0 __asm__("a0") = status;
    register long a7 __asm__("a7") = SYS_EXIT;
    __asm__ volatile("ecall" ::"r"(a0), "r"(a7) : "memory");
}

void exit(int status)
{
    libc_ecall_exit(status);
    for (;;) {
    }
}

void abort(void)
{
    libc_ecall_exit(20);
    for (;;) {
    }
}
