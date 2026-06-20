// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the `syscall`-flavour dos.library
// LVOs. (The packet ops — Open/Read/Lock/Examine/… — are `server`
// flavour and run in the Logaic dos handler Gleas, not here; they arrive
// with later L3 slices.) Routed from src/croi/syscall/syscall.c via the
// per-LVO trampoline in src/logaic/dos/trampolines.S.

#ifndef CARA_DOS_LIB_H
#define CARA_DOS_LIB_H

#include <cara/types.h>
#include <exec/types.h>

// IoErr — the calling Process's pr_Result2. The kernel reads
// Sched_Current() and casts to struct Process * (every U-mode Gleas's
// Task is embedded at the front of a shared-heap Process; see
// docs/LOGAIC_DOS.md §2.2).
LONG Croi_Dos_IoErr_Impl(void);

#endif // CARA_DOS_LIB_H
