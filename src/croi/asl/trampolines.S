// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for asl.library (Phase 3 L9, LVO.md §5.2).
// Each sets a7 = SYS_Asl_* and ecalls into Croi; the generated asl_vec.c
// points each `syscall` vec slot here. Section `.lib_text.asl` is folded
// by croi.lds into the shared 0x4000_0000 library RX page (like the other
// libraries); asl.library's base + vec live in the SASOS shared heap.

#include <cara/sysno.h>

#include <cara/arch/syscall_trampoline.inc>

.section .lib_text.asl, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Asl_AllocAslRequest,  SYS_Asl_AllocAslRequest
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Asl_FreeAslRequest,   SYS_Asl_FreeAslRequest
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Asl_FreeFileRequest,  SYS_Asl_FreeFileRequest
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Asl_AslRequest,       SYS_Asl_AslRequest
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Asl_AllocFileRequest, SYS_Asl_AllocFileRequest
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Asl_RequestFile,      SYS_Asl_RequestFile
