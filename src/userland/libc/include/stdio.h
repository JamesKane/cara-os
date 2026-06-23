/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * <stdio.h> — CaraOS userland libc (Phase T). Phase T.1 covers the
 * formatted-output family (printf/sprintf/snprintf/puts/putchar) backed
 * by the kernel console; stdout/stderr are sentinels. Disk FILE streams
 * (fopen/fread/fgets over dos) arrive when a port needs them (T.2).
 * docs/PORTS.md §1.1. (Implemented in src/userland/libc/stdio.c + fmt.c.)
 */

#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

typedef struct _CARA_FILE FILE;

extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(FILE *stream, const char *fmt, va_list ap);

int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsprintf(char *str, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

int puts(const char *s);
int fputs(const char *s, FILE *stream);
int putchar(int c);
int fputc(int c, FILE *stream);
int putc(int c, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);

#endif /* _STDIO_H */
