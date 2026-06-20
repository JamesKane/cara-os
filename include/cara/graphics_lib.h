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

#endif // CARA_GRAPHICS_LIB_H
