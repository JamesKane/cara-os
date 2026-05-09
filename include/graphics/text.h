// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <graphics/text.h> — font selection and metrics.
// struct TextAttr is small and stable; defined fully here so V36+
// source that opens fonts via OpenFont(&attr) compiles. struct
// TextFont (the live font handle) is forward-declared until
// graphics.library lands in Phase 3.
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition, graphics/text.h.

#ifndef GRAPHICS_TEXT_H
#define GRAPHICS_TEXT_H

#include <exec/types.h>

// V36+ ta_Style bits.
#define FS_NORMAL 0
#define FSF_UNDERLINED (1 << 0)
#define FSF_BOLD (1 << 1)
#define FSF_ITALIC (1 << 2)
#define FSF_EXTENDED (1 << 3)
#define FSF_TAGGED (1 << 6)

// V36+ ta_Flags bits.
#define FPF_ROMFONT (1 << 0)
#define FPF_DISKFONT (1 << 1)
#define FPF_REVPATH (1 << 2)
#define FPF_TALLDOT (1 << 3)
#define FPF_WIDEDOT (1 << 4)
#define FPF_PROPORTIONAL (1 << 5)
#define FPF_DESIGNED (1 << 6)
#define FPF_REMOVED (1 << 7)

struct TextAttr {
    STRPTR ta_Name;
    UWORD ta_YSize;
    UBYTE ta_Style;
    UBYTE ta_Flags;
};

struct TextFont; // forward; full type lands with Phase 3.

#endif // GRAPHICS_TEXT_H
