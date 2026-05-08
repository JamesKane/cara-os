// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-internal globals shared between subsystems (entry, tests, etc.).
// Not exported under include/cara/ — only files inside src/croi/ should
// pull these in.

#ifndef CARA_CROI_KERNEL_H
#define CARA_CROI_KERNEL_H

#include "ns16550.h"

#include <cara/alloc.h>
#include <cara/mm.h>

extern struct PageAllocator g_page_alloc;
extern struct Heap          g_heap;
extern struct Ns16550       g_console_uart;

#endif
