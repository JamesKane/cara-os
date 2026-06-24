// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for dos.library's `syscall`-flavour rows
// (L3.1: just IoErr; the packet ops are `server` flavour and don't
// trampoline here). Like the exec/intuition/utility trampolines these
// live in the shared RX page at 0x4000_0000 (section .lib_text.dos,
// placed by croi.lds). The generated dos_vec.c points the IoErr vec slot
// at this; calling IoErr() via <proto/dos.h> reaches here, the trampoline
// ecalls into Croi, and src/croi/syscall/syscall.c routes by a7.

#include <cara/sysno.h>

.macro CARA_SYSCALL_TRAMPOLINE name, num
    .global \name
    .type   \name, @function
    \name:
        li      a7, \num
        ecall
        ret
    .size   \name, . - \name
.endm

.section .lib_text.dos, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosIoErr,      SYS_Dos_IoErr
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosLock,       SYS_Dos_Lock
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosUnLock,     SYS_Dos_UnLock
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosDupLock,    SYS_Dos_DupLock
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosCurrentDir, SYS_Dos_CurrentDir
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosExamine,    SYS_Dos_Examine
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosExNext,     SYS_Dos_ExNext
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosOpen,       SYS_Dos_Open
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosClose,      SYS_Dos_Close
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosRead,       SYS_Dos_Read
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosWrite,      SYS_Dos_Write
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosSeek,       SYS_Dos_Seek
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosDeleteFile, SYS_Dos_DeleteFile
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosRename,     SYS_Dos_Rename
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosInfo,       SYS_Dos_Info
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosCreateDir,  SYS_Dos_CreateDir
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosInput,      SYS_Dos_Input
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosOutput,     SYS_Dos_Output
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosDelay,      SYS_Dos_Delay
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosLoadSeg,    SYS_Dos_LoadSeg
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosUnLoadSeg,  SYS_Dos_UnLoadSeg
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_DosRunCommand, SYS_Dos_RunCommand
