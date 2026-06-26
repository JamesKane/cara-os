// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for exec.library (Phase 3, LVO.md §5.2).
//
// Each trampoline below is a tiny U-mode-executable stub that lives
// in the library's RX page at 0x0000_0000_4000_0000+ (section
// `.lib_text.exec`, placed by the linker — see src/croi/croi.lds).
// The vec table slot for each `syscall`-flavour LVO points at the
// trampoline; calling the LVO via the inline stub from <proto/exec.h>
// reaches here, the trampoline ecalls into Croi, and the dispatcher
// in src/croi/syscall/syscall.c routes by a7 = syscall number.
//
// Calling convention: the inline stub passes user args verbatim in
// a0..a5 (followed by SysBase as the trailing arg, ignored by syscall
// flavour). The trampoline preserves them, sets a7 = SYS_<Name>, and
// ecalls. Return value comes back in a0; `ret` returns to the inline
// stub. Net dispatch cost: 3 instructions in U-mode plus the trap.

#include <cara/sysno.h>

#include <cara/arch/syscall_trampoline.inc>

// `.lib_text.exec` is the section the linker places at the
// 0x4000_0000 library RX page (C7). On hosts where the linker script
// hasn't been updated yet, the section just lands wherever the
// default placement falls — code still works for kernel-side smoke
// tests because S-mode can call its own ecall path without minding
// the page user-bit.
.section .lib_text.exec, "ax"
.balign 4

// ---- Library lifecycle ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_OpenLibrary,     SYS_OpenLibrary
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_OldOpenLibrary,  SYS_OldOpenLibrary
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_CloseLibrary,    SYS_CloseLibrary

// ---- Memory ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_AllocMem,        SYS_AllocMem
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_FreeMem,         SYS_FreeMem
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_AllocVec,        SYS_AllocVec
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_FreeVec,         SYS_FreeVec

// ---- Signals ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Wait,            SYS_Wait
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Signal,          SYS_Signal
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_AllocSignal,     SYS_AllocSignal
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_FreeSignal,      SYS_FreeSignal
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_SetSignal,       SYS_SetSignal

// ---- Tasks ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_FindTask,        SYS_FindTask

// ---- Task-switch control ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Disable,         SYS_Disable
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Enable,          SYS_Enable
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Forbid,          SYS_Forbid
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Permit,          SYS_Permit

// ---- Semaphores ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_InitSemaphore,    SYS_InitSemaphore
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ObtainSemaphore,  SYS_ObtainSemaphore
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ReleaseSemaphore, SYS_ReleaseSemaphore
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_AttemptSemaphore, SYS_AttemptSemaphore

// ---- IPC ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_PutMsg,          SYS_PutMsg
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GetMsg,          SYS_GetMsg
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_WaitPort,        SYS_WaitPort
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_ReplyMsg,        SYS_ReplyMsg
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_CreateMsgPort,   SYS_CreateMsgPort
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DeleteMsgPort,   SYS_DeleteMsgPort

// ---- Devices (L6) ----
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_OpenDevice,      SYS_OpenDevice
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_CloseDevice,     SYS_CloseDevice
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DoIO,            SYS_DoIO
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_SendIO,          SYS_SendIO
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_CheckIO,         SYS_CheckIO
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_WaitIO,          SYS_WaitIO
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_AbortIO,         SYS_AbortIO
