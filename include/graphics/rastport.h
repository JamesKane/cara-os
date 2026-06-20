// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <graphics/rastport.h> — drawing surface state. The full
// V36+ definition lands with L4.2 (docs/DATH_GRAPHICS.md). struct Screen
// (intuition/screens.h) holds a `struct RastPort *`.
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition,
// graphics/rastport.h.

#ifndef GRAPHICS_RASTPORT_H
#define GRAPHICS_RASTPORT_H

#include <exec/types.h>

struct Layer;    // <graphics/layers.h>
struct TmpRas;   // areafill scratch (unused on CaraOS — chunky fill)
struct GelsInfo; // sprite/GEL state (unused on CaraOS — no chipset)
struct BitMap;   // <graphics/gfx.h>
struct TextFont; // <graphics/text.h>

// V36+ struct AreaInfo (graphics/rastport.h) — the vertex accumulator for
// the Area* polygon-fill calls. InitArea points VctrTbl/FlagTbl at a
// caller buffer (5 bytes/vector: 2 WORDs coords + 1 flag byte); AreaMove/
// AreaDraw append; AreaEnd scanline-fills and resets. The app sets
// rp->AreaInfo to one of these. See docs/DATH_GRAPHICS.md §5 (L4.6).
struct AreaInfo {
    WORD *VctrTbl; // base of the coordinate table (2 WORDs per vector)
    WORD *VctrPtr; // next free coordinate slot
    BYTE *FlagTbl; // base of the per-vector flag table
    BYTE *FlagPtr; // next free flag slot
    WORD Count;    // vectors accumulated so far
    WORD MaxCount; // capacity (from InitArea)
    WORD FirstX;   // first point of the current polygon
    WORD FirstY;
};

// V36+ DrawMode (SetDrMd): JAM1 draws FgPen only, JAM2 draws Fg over Bg,
// COMPLEMENT XORs, INVERSVID swaps fg/bg for text.
#define JAM1 0
#define JAM2 1
#define COMPLEMENT 2
#define INVERSVID 4

// V36+ struct RastPort — the drawing-state object. Field order/offsets
// are ABI. The caller owns it (stack or shared heap); InitRastPort sets
// the defaults and the graphics LVOs read/update it (DATH_GRAPHICS.md
// §2.3). FgPen/BgPen are pen *indices* into the v0 default palette (the
// minimal ColorMap); cp_x/cp_y track the graphics cursor for Move/Draw.
struct RastPort {
    struct Layer *Layer;
    struct BitMap *BitMap;
    UWORD *AreaPtr;
    struct TmpRas *TmpRas;
    struct AreaInfo *AreaInfo;
    struct GelsInfo *GelsInfo;
    UBYTE Mask;
    BYTE FgPen;
    BYTE BgPen;
    BYTE AOlPen;
    BYTE DrawMode;
    BYTE AreaPtSz;
    BYTE linpatcnt;
    BYTE dummy;
    UWORD Flags;
    UWORD LinePtrn;
    WORD cp_x, cp_y; // current pen position
    UBYTE minterms[8];
    WORD PenWidth;
    WORD PenHeight;
    struct TextFont *Font;
    UBYTE AlgoStyle;
    UBYTE TxFlags;
    UWORD TxHeight;
    UWORD TxWidth;
    UWORD TxBaseline;
    WORD TxSpacing;
    APTR *RP_User;
    ULONG longreserved[2];
    UWORD wordreserved[7];
    UBYTE reserved[8];
};

#endif // GRAPHICS_RASTPORT_H
