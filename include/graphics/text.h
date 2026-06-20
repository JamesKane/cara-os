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

#include <exec/ports.h>
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

// V36+ struct TextFont (graphics/text.h) — a live font handle. Field
// order/offsets are ABI. On CaraOS the AmigaOS strike-format char data
// (tf_CharData/tf_CharLoc/…) is not populated in v0 — graphics.library
// has one face, the Dath 8x8 bitmap font, and the rasteriser renders
// from it directly; this struct is the descriptor apps read (tf_YSize/
// tf_XSize/tf_Baseline) and the handle SetFont/Text take. See
// docs/DATH_GRAPHICS.md §5 (L4.5).
struct TextFont {
    struct Message tf_Message; // ln_Name = font name; on the font list
    UWORD tf_YSize;            // font height (pixels)
    UBYTE tf_Style;            // FS*/FSF_* style bits
    UBYTE tf_Flags;            // FPF_* flags
    UWORD tf_XSize;            // nominal width (pixels) — monospace in v0
    UWORD tf_Baseline;         // distance from top to baseline
    UWORD tf_BoldSmear;        // pixels to smear for algorithmic bold
    UWORD tf_Accessors;        // open count
    UBYTE tf_LoChar;           // first glyph codepoint
    UBYTE tf_HiChar;           // last glyph codepoint
    APTR tf_CharData;          // strike data (unused in v0)
    UWORD tf_Modulo;           // bytes per row of strike (unused)
    APTR tf_CharLoc;           // per-glyph bit offset/width (unused)
    APTR tf_CharSpace;         // per-glyph spacing (unused)
    APTR tf_CharKern;          // per-glyph kerning (unused)
};

#endif // GRAPHICS_TEXT_H
