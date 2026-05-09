// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <graphics/gfx.h> — the bedrock graphics types. Phase 1
// scaffolding: only Rectangle (small, V36+-canonical) and PLANEPTR are
// defined fully; BitMap is forward-declared because Phase 1's screen
// substrate addresses it through pointers per the LB pragmatic
// deviation (see docs/PHASE1_LEARGAS.md). Phase 3 fills BitMap out
// when graphics.library lands.
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition, graphics/gfx.h.

#ifndef GRAPHICS_GFX_H
#define GRAPHICS_GFX_H

#include <exec/types.h>

// V36+ pointer-to-bit-plane. On the Amiga this addresses Chip RAM
// pixel planes; CaraOS's Dath surfaces are linear-pixel framebuffers,
// so this typedef exists for source compatibility only.
typedef UBYTE *PLANEPTR;

// V36+ canonical rectangle. Inclusive corners — MinX..MaxX and
// MinY..MaxY both inclusive of their endpoints.
struct Rectangle {
    WORD MinX, MinY;
    WORD MaxX, MaxY;
};

// Forward declaration; full type lands with Phase 3 graphics.library.
struct BitMap;

#endif // GRAPHICS_GFX_H
