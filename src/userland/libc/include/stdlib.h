/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * <stdlib.h> — CaraOS userland libc (Phase T). malloc/free over exec
 * AllocVec/FreeVec; integer conversion + qsort + exit. docs/PORTS.md §1.1.
 * (Implemented in src/userland/libc/stdlib.c.)
 */

#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 20
#define RAND_MAX 0x7fffffff

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

int atoi(const char *s);
long atol(const char *s);
long strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);

int abs(int n);
long labs(long n);

void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *));

int rand(void);
void srand(unsigned seed);

void exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

#endif /* _STDLIB_H */
