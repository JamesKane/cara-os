// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for graphics.library's `syscall`-flavour
// rows (L4.2+). Like the exec/intuition/utility/dos trampolines these
// live in the shared RX page at 0x4000_0000 (section .lib_text.graphics,
// placed by croi.lds). The generated graphics_vec.c points the vec slots
// at these; calling e.g. SetRast() via <proto/graphics.h> reaches here,
// the trampoline ecalls into Croi, and src/croi/syscall/syscall.c routes
// by a7.

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

.section .lib_text.graphics, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxAllocBitMap,   SYS_Gfx_AllocBitMap
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxFreeBitMap,    SYS_Gfx_FreeBitMap
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxInitRastPort,  SYS_Gfx_InitRastPort
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxSetRast,       SYS_Gfx_SetRast
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxMove,          SYS_Gfx_Move
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxDraw,          SYS_Gfx_Draw
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxRectFill,      SYS_Gfx_RectFill
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxReadPixel,     SYS_Gfx_ReadPixel
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxWritePixel,    SYS_Gfx_WritePixel
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxSetAPen,       SYS_Gfx_SetAPen
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxSetBPen,       SYS_Gfx_SetBPen
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxSetDrMd,       SYS_Gfx_SetDrMd
