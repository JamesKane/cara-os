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

// V36+ struct BitMap (graphics/gfx.h). The field order/offsets are ABI.
// On the Amiga this describes up to 8 planar bit-planes; on CaraOS,
// graphics.library is chunky/RTG (docs/DATH_GRAPHICS.md §2.1), so a
// drawable BitMap is the *opaque head* of a kernel-private DathBitMapExt
// that carries the real chunky surface — BytesPerRow/Rows/Depth are
// filled best-effort and Planes[0] points at the chunky pixel buffer
// (shared-heap; readable from U-mode). Allocate via AllocBitMap; peeking
// Planes[] for planar layout is out of scope.
struct BitMap {
    UWORD BytesPerRow;
    UWORD Rows;
    UBYTE Flags;
    UBYTE Depth;
    UWORD pad;
    PLANEPTR Planes[8];
};

// AllocBitMap flags (V39 graphics/gfx.h). v0 honours BMF_CLEAR.
#define BMF_CLEAR (1 << 0)
#define BMF_DISPLAYABLE (1 << 1)
#define BMF_INTERLEAVED (1 << 2)
#define BMF_STANDARD (1 << 3)
#define BMF_MINPLANES (1 << 4)

#endif // GRAPHICS_GFX_H
