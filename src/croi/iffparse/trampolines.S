// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for iffparse.library (Phase 3 L10, LVO.md
// §5.2). Each sets a7 = SYS_Iff_* and ecalls into Croi; the generated
// iffparse_vec.c points each `syscall` vec slot here. Section
// `.lib_text.iffparse` is folded by croi.lds into the shared 0x4000_0000
// library RX page; iffparse.library's base + vec live in the SASOS shared
// heap.

#include <cara/sysno.h>

#include <cara/arch/syscall_trampoline.inc>

.section .lib_text.iffparse, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_AllocIFF,     SYS_Iff_AllocIFF
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_OpenIFF,      SYS_Iff_OpenIFF
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_CloseIFF,     SYS_Iff_CloseIFF
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_FreeIFF,      SYS_Iff_FreeIFF
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_InitIFFasDOS, SYS_Iff_InitIFFasDOS
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_ParseIFF,     SYS_Iff_ParseIFF
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_ReadChunkBytes,   SYS_Iff_ReadChunkBytes
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_ReadChunkRecords, SYS_Iff_ReadChunkRecords
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_StopChunk,    SYS_Iff_StopChunk
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_StopOnExit,   SYS_Iff_StopOnExit
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_CurrentChunk, SYS_Iff_CurrentChunk
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_ParentChunk,  SYS_Iff_ParentChunk
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_PushChunk,    SYS_Iff_PushChunk
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_PopChunk,     SYS_Iff_PopChunk
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_WriteChunkBytes,   SYS_Iff_WriteChunkBytes
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_WriteChunkRecords, SYS_Iff_WriteChunkRecords
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_PropChunk,        SYS_Iff_PropChunk
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_PropChunks,       SYS_Iff_PropChunks
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_CollectionChunk,  SYS_Iff_CollectionChunk
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_CollectionChunks, SYS_Iff_CollectionChunks
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_FindProp,         SYS_Iff_FindProp
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Iff_FindCollection,   SYS_Iff_FindCollection
