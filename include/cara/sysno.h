// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS syscall numbers — a stable wire ABI between user-mode
// trampolines (in 0x4000_0000 library RX pages) and the Croi
// dispatcher (src/croi/syscall/syscall.c). Numbers are allocated by
// phase: Phase 1 (boot smoke) starts at 1, Phase 3 (exec.library)
// continues from 3, future phases append.
//
// User-mode never references these numbers directly — every call goes
// through a per-LVO trampoline (Cara_Trampoline_<Name> in
// src/croi/exec_lib/trampolines.S) that does `li a7, <number>; ecall;
// ret`. The numbers must stay stable so trampolines compiled into a
// library RX page keep working across kernel updates.

#ifndef CARA_SYSNO_H
#define CARA_SYSNO_H

// Phase 1 — kernel smoke / log surface.
#define SYS_LOG_WRITE     1
#define SYS_EXIT          2

// Phase 3 — exec.library.
//
// OpenLibrary and OldOpenLibrary have separate numbers so the
// dispatcher can route OldOpenLibrary directly through the same impl
// with version forced to 0, without needing a custom trampoline.
#define SYS_OpenLibrary    3
#define SYS_CloseLibrary   4
#define SYS_OldOpenLibrary 5
#define SYS_AllocMem       6
#define SYS_FreeMem        7
#define SYS_Wait           8
#define SYS_Signal         9
#define SYS_AllocSignal   10
#define SYS_FreeSignal    11
#define SYS_SetSignal     12
#define SYS_PutMsg        13
#define SYS_GetMsg        14
#define SYS_WaitPort      15

#endif
