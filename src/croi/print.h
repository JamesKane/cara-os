// SPDX-License-Identifier: BSD-2-Clause
//
// Croi print: tiny printf-flavoured logger with a swappable byte-emit
// backend. Default backend is sbi_putc; once a real UART driver is up,
// the platform code installs its putc as the new backend.

#ifndef CARA_CROI_PRINT_H
#define CARA_CROI_PRINT_H

#include <cara/attr.h>
#include <cara/types.h>

typedef void (*Croi_PutcFn)(char);

void Croi_PrintInstallBackend(Croi_PutcFn put);
void Croi_PutByte(char c);

void Croi_Print(const char *fmt, ...);

CARA_NORETURN void Croi_Panic(const char *msg);

#endif
