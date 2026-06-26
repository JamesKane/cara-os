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

#include <cara/arch/syscall_trampoline.inc>

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
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxTextLength,    SYS_Gfx_TextLength
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxText,          SYS_Gfx_Text
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxSetFont,       SYS_Gfx_SetFont
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxOpenFont,      SYS_Gfx_OpenFont
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxCloseFont,     SYS_Gfx_CloseFont
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxInitArea,      SYS_Gfx_InitArea
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxAreaMove,      SYS_Gfx_AreaMove
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxAreaDraw,      SYS_Gfx_AreaDraw
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_GfxAreaEnd,       SYS_Gfx_AreaEnd
