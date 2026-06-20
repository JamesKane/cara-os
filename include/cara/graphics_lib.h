// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes + bridge type for the `syscall`-flavour
// graphics.library LVOs (L4). Routed from src/croi/syscall/syscall.c via
// the per-LVO trampolines in src/croi/dath/trampolines.S (the
// .lib_text.graphics RX page). The impls (src/croi/dath/graphics_lib.c)
// marshal the V36 RastPort/BitMap calls onto the Dath_* rasteriser.
// Design: docs/DATH_GRAPHICS.md.

#ifndef CARA_GRAPHICS_LIB_H
#define CARA_GRAPHICS_LIB_H

#include <cara/dath.h>
#include <cara/types.h>
#include <exec/types.h>
#include <graphics/gfx.h>

struct RastPort;

// A drawable BitMap is the head of this kernel-private extension: the
// public struct BitMap (offset 0, the pointer the app holds) plus the
// real chunky Dath surface. AllocBitMap allocates one in the SASOS
// shared heap so the pixels are readable/writable from U-mode; the
// rasteriser recovers `surf` from any BitMap * (docs/DATH_GRAPHICS.md
// §2.1). Mirrors the dos DosLockExt/DosFileExt pattern.
struct DathBitMapExt {
    struct BitMap bm;            // offset 0 — the BitMap * points here
    struct DathFramebuffer surf; // the real chunky pixels
};

// ---- BitMap allocation + RastPort init (L4.2) ------------------------
struct BitMap *Croi_Gfx_AllocBitMap_Impl(ULONG sizex, ULONG sizey, ULONG depth, ULONG flags,
                                         const struct BitMap *friend_bitmap);
void Croi_Gfx_FreeBitMap_Impl(struct BitMap *bm);
void Croi_Gfx_InitRastPort_Impl(struct RastPort *rp);
void Croi_Gfx_SetRast_Impl(struct RastPort *rp, ULONG pen);

// ---- Pen state + primitives (L4.3) -----------------------------------
void Croi_Gfx_Move_Impl(struct RastPort *rp, WORD x, WORD y);
void Croi_Gfx_Draw_Impl(struct RastPort *rp, WORD x, WORD y);
void Croi_Gfx_RectFill_Impl(struct RastPort *rp, WORD xMin, WORD yMin, WORD xMax, WORD yMax);
LONG Croi_Gfx_ReadPixel_Impl(struct RastPort *rp, WORD x, WORD y);
LONG Croi_Gfx_WritePixel_Impl(struct RastPort *rp, WORD x, WORD y);
void Croi_Gfx_SetAPen_Impl(struct RastPort *rp, ULONG pen);
void Croi_Gfx_SetBPen_Impl(struct RastPort *rp, ULONG pen);
void Croi_Gfx_SetDrMd_Impl(struct RastPort *rp, ULONG mode);

// ---- Blits (L4.4) ----------------------------------------------------
// The wide V36 blits (BltBitMap 11 args, BltBitMapRastPort/ClipBlit 9)
// exceed the 7-register syscall ABI, so they are `local` marshalling
// stubs (src/croi/dath/graphics_blit.c, .lib_text.graphics) that resolve
// their operands to source/dest BitMaps, pack this struct, and make one
// SYS_Gfx_Blt ecall. The kernel impl runs the chunky copy (Dath_BlitRect,
// same-format only — see §3/§6.4). Plain copy in v0 (minterm ignored).
struct GfxBltArgs {
    const struct BitMap *src;
    struct BitMap *dest;
    WORD xSrc, ySrc;
    WORD xDest, yDest;
    WORD xSize, ySize;
};
LONG Croi_Gfx_Blt_Impl(const struct GfxBltArgs *a);

// ---- Text + fonts (L4.5) ---------------------------------------------
struct TextFont;
struct TextAttr;
LONG Croi_Gfx_TextLength_Impl(struct RastPort *rp, STRPTR string, ULONG count);
void Croi_Gfx_Text_Impl(struct RastPort *rp, STRPTR string, ULONG count);
void Croi_Gfx_SetFont_Impl(struct RastPort *rp, struct TextFont *textFont);
struct TextFont *Croi_Gfx_OpenFont_Impl(struct TextAttr *textAttr);
void Croi_Gfx_CloseFont_Impl(struct TextFont *textFont);

// ---- Area* polygon fill (L4.6) ---------------------------------------
struct AreaInfo;
void Croi_Gfx_InitArea_Impl(struct AreaInfo *areaInfo, APTR vectorBuffer, WORD maxVectors);
LONG Croi_Gfx_AreaMove_Impl(struct RastPort *rp, WORD x, WORD y);
LONG Croi_Gfx_AreaDraw_Impl(struct RastPort *rp, WORD x, WORD y);
LONG Croi_Gfx_AreaEnd_Impl(struct RastPort *rp);

#endif // CARA_GRAPHICS_LIB_H
