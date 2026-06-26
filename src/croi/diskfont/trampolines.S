// SPDX-License-Identifier: BSD-2-Clause
//
// Per-LVO syscall trampolines for diskfont.library (Phase 3 L12, LVO.md
// §5.2). Each sets a7 = SYS_Diskfont_* and ecalls into Croi; the generated
// diskfont_vec.c points each `syscall` vec slot here. Section
// `.lib_text.diskfont` is folded by croi.lds into the shared 0x4000_0000
// library RX page; diskfont.library's base + vec live in the SASOS shared
// heap.

#include <cara/sysno.h>

#include <cara/arch/syscall_trampoline.inc>

.section .lib_text.diskfont, "ax"
.balign 4

CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Diskfont_OpenDiskFont,        SYS_Diskfont_OpenDiskFont
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Diskfont_AvailFonts,          SYS_Diskfont_AvailFonts
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Diskfont_NewFontContents,     SYS_Diskfont_NewFontContents
CARA_SYSCALL_TRAMPOLINE Cara_Trampoline_Diskfont_DisposeFontContents, SYS_Diskfont_DisposeFontContents
