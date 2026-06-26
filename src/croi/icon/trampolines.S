// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for icon.library (Phase 3 L11, LVO.md
// §5.2). Each sets a7 = SYS_Icon_* and ecalls into Croi; the generated
// icon_vec.c points each `syscall` vec slot here. Section
// `.lib_text.icon` is folded by croi.lds into the shared 0x4000_0000
// library RX page; icon.library's base + vec live in the SASOS shared
// heap.

#include <cara/sysno.h>

#include <cara/arch/syscall_trampoline.inc>

.section .lib_text.icon, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_GetDiskObject,    SYS_Icon_GetDiskObject
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_FreeDiskObject,   SYS_Icon_FreeDiskObject
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_PutDiskObject,    SYS_Icon_PutDiskObject
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_DeleteDiskObject, SYS_Icon_DeleteDiskObject
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_GetDiskObjectNew, SYS_Icon_GetDiskObjectNew
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_GetDefDiskObject, SYS_Icon_GetDefDiskObject
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Icon_PutDefDiskObject, SYS_Icon_PutDefDiskObject
