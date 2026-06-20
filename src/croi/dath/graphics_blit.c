// SPDX-License-Identifier: BSD-2-Clause
//
// graphics.library wide-blit marshalling stubs (L4.4). The V36 blits take
// more arguments than the 7-register syscall ABI allows (BltBitMap 11,
// BltBitMapRastPort/ClipBlit 9 — the trampoline needs a7 for the syscall
// number), so these are `local`-flavour: thin functions that live in the
// .lib_text.graphics RX page (mapped at user VA 0x4000_0000), run in
// U-mode, resolve their operands to source/dest BitMaps, pack a GfxBltArgs
// on the stack, and make ONE SYS_Gfx_Blt ecall passing &args in a0. The
// kernel reads the struct with SUM=1 and runs Croi_Gfx_Blt_Impl.
//
// SELF-CONTAINED, like utility's tag_ops.c: no calls out of the section
// (a cross-section call would PC-rel-overflow); only inline field stores
// and the inline ecall. See docs/DATH_GRAPHICS.md §2.2 and the L1 RX-page
// notes.

#include <cara/graphics_lib.h>
#include <cara/sysno.h>
#include <cara/types.h>
#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>

#define LIBTEXT_G __attribute__((section(".lib_text.graphics"), used))

// The SYS_Gfx_Blt ecall, inlined directly into each stub (no shared
// helper — a non-inlined static would be an out-of-section call from the
// RX page and PC-rel-overflow; cf. the L1 RX-page notes). `args` is the
// caller's stack GfxBltArgs; the kernel reads it via the a0 pointer.
#define GFX_BLT_ECALL(argsp, retvar)                                                               \
    do {                                                                                           \
        register long _a0 __asm__("a0") = (long)(uptr)(argsp);                                     \
        register long _a7 __asm__("a7") = SYS_Gfx_Blt;                                             \
        __asm__ volatile("ecall" : "+r"(_a0) : "r"(_a7) : "memory");                               \
        (retvar) = _a0;                                                                            \
    } while (0)

// BltBitMap — BitMap → BitMap. Plain copy in v0 (minterm/mask/tempA
// ignored). Returns 0 (chunky surfaces have no plane count).
LIBTEXT_G LONG Croi_Gfx_BltBitMap(const struct BitMap *srcBitMap, WORD xSrc, WORD ySrc,
                                  struct BitMap *destBitMap, WORD xDest, WORD yDest, WORD xSize,
                                  WORD ySize, UBYTE minterm, UBYTE mask, PLANEPTR tempA)
{
    (void)minterm;
    (void)mask;
    (void)tempA;
    struct GfxBltArgs args;
    args.src = srcBitMap;
    args.dest = destBitMap;
    args.xSrc = xSrc;
    args.ySrc = ySrc;
    args.xDest = xDest;
    args.yDest = yDest;
    args.xSize = xSize;
    args.ySize = ySize;
    long ret;
    GFX_BLT_ECALL(&args, ret);
    return (LONG)ret;
}

// BltBitMapRastPort — BitMap → RastPort's BitMap.
LIBTEXT_G void Croi_Gfx_BltBitMapRastPort(struct BitMap *srcBitMap, WORD xSrc, WORD ySrc,
                                          struct RastPort *destRP, WORD xDest, WORD yDest,
                                          WORD xSize, WORD ySize, UBYTE minterm)
{
    (void)minterm;
    struct GfxBltArgs args;
    args.src = srcBitMap;
    args.dest = destRP ? destRP->BitMap : nullptr;
    args.xSrc = xSrc;
    args.ySrc = ySrc;
    args.xDest = xDest;
    args.yDest = yDest;
    args.xSize = xSize;
    args.ySize = ySize;
    long ret;
    GFX_BLT_ECALL(&args, ret);
    (void)ret;
}

// ClipBlit — RastPort → RastPort (both resolved to their BitMaps). v0
// clips to the BitMap rect (no layer/clip regions yet — §6.4).
LIBTEXT_G void Croi_Gfx_ClipBlit(struct RastPort *srcRP, WORD xSrc, WORD ySrc,
                                 struct RastPort *destRP, WORD xDest, WORD yDest, WORD xSize,
                                 WORD ySize, UBYTE minterm)
{
    (void)minterm;
    struct GfxBltArgs args;
    args.src = srcRP ? srcRP->BitMap : nullptr;
    args.dest = destRP ? destRP->BitMap : nullptr;
    args.xSrc = xSrc;
    args.ySrc = ySrc;
    args.xDest = xDest;
    args.yDest = yDest;
    args.xSize = xSize;
    args.ySize = ySize;
    long ret;
    GFX_BLT_ECALL(&args, ret);
    (void)ret;
}
