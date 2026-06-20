// SPDX-License-Identifier: BSD-2-Clause
//
// graphics.library `syscall`-flavour impls (L4.2) — BitMap allocation +
// RastPort init. The U-mode trampoline (trampolines.S, .lib_text.
// graphics) ecalls into Croi, the dispatcher routes to these, and they
// marshal the V36 RastPort/BitMap model onto the Dath_* rasteriser
// (docs/DATH_GRAPHICS.md §2 / §3).

#include <cara/alloc.h>
#include <cara/dath.h>
#include <cara/graphics_lib.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>

// v0 default palette — the minimal ColorMap (docs/DATH_GRAPHICS.md §6.2).
// Pen indices map to packed 0xRRGGBB; converted to the target surface
// format at draw time. A real ColorMap (SetRGB4/LoadRGB4) overrides this
// when an app needs a palette. Out-of-range pens fall back to pen 0.
static const u32 g_default_palette[] = {
    0x000000, // 0 black
    0xFFFFFF, // 1 white
    0xFF0000, // 2 red
    0x00FF00, // 3 green
    0x0000FF, // 4 blue
    0x00FFFF, // 5 cyan
    0xFF00FF, // 6 magenta
    0xFFFF00, // 7 yellow
};
static constexpr u32 GFX_PALETTE_N = sizeof(g_default_palette) / sizeof(g_default_palette[0]);

// Resolve a pen index through the default palette into a surface-format
// DathColor.
static DathColor pen_to_color(ULONG pen, const struct DathFramebuffer *fb)
{
    u32 rgb = g_default_palette[pen < GFX_PALETTE_N ? pen : 0];
    u8 r = (u8)(rgb >> 16);
    u8 g = (u8)(rgb >> 8);
    u8 b = (u8)rgb;
    return (fb->format == DATH_FMT_RGB565) ? Dath_RGB565(r, g, b) : Dath_RGB(r, g, b);
}

// Recover the chunky Dath surface from a drawable BitMap (always the head
// of a DathBitMapExt — §2.1).
static struct DathFramebuffer *surf_of(struct BitMap *bm)
{
    return bm ? &((struct DathBitMapExt *)bm)->surf : nullptr;
}

// AllocBitMap(sizex, sizey, depth, flags, friend) — a chunky drawable
// BitMap in the shared heap (U-mode-readable pixels). v0 maps depth to a
// format: <=16 → RGB565, else RGBA8888 (§2.1). Returns BitMap * or 0.
struct BitMap *Croi_Gfx_AllocBitMap_Impl(ULONG sizex, ULONG sizey, ULONG depth, ULONG flags,
                                         const struct BitMap *friend_bitmap)
{
    (void)friend_bitmap;
    if (sizex == 0 || sizey == 0) {
        return nullptr;
    }
    DathFormat fmt = (depth <= 16) ? DATH_FMT_RGB565 : DATH_FMT_RGBA8888;
    u32 bpp = (fmt == DATH_FMT_RGB565) ? 2u : 4u;
    usize px_size = (usize)sizex * (usize)sizey * (usize)bpp;

    struct DathBitMapExt *ext = (struct DathBitMapExt *)Croi_AllocShared(sizeof(*ext));
    if (!ext) {
        return nullptr;
    }
    void *px = Croi_AllocShared(px_size);
    if (!px) {
        Croi_Free(ext);
        return nullptr;
    }
    if (Dath_Framebuffer_Init(&ext->surf, px, sizex, sizey, sizex * bpp, fmt) != CARA_EOK) {
        Croi_Free(px);
        Croi_Free(ext);
        return nullptr;
    }

    ext->bm = (struct BitMap){ 0 };
    ext->bm.BytesPerRow = (UWORD)(sizex * bpp);
    ext->bm.Rows = (UWORD)sizey;
    ext->bm.Depth = (UBYTE)depth;
    ext->bm.Planes[0] = (PLANEPTR)px; // chunky buffer lives in Planes[0]

    if (flags & BMF_CLEAR) {
        Dath_Clear(&ext->surf, 0); // 0 == black/transparent in every format
    }
    return &ext->bm;
}

// FreeBitMap(bm) — release a BitMap from AllocBitMap. 0 is a no-op.
void Croi_Gfx_FreeBitMap_Impl(struct BitMap *bm)
{
    if (!bm) {
        return;
    }
    struct DathBitMapExt *ext = (struct DathBitMapExt *)bm; // bm at offset 0
    if (ext->surf.base) {
        Croi_Free(ext->surf.base);
    }
    Croi_Free(ext);
}

// InitRastPort(rp) — zero the caller's RastPort and set the V36 defaults.
// No Dath call; rp is a user pointer written with SUM=1.
void Croi_Gfx_InitRastPort_Impl(struct RastPort *rp)
{
    if (!rp) {
        return;
    }
    *rp = (struct RastPort){ 0 };
    rp->Mask = 0xFF;
    rp->FgPen = 1; // default APen = white (palette index 1)
    rp->BgPen = 0; // default BPen = black
    rp->DrawMode = JAM2;
    rp->LinePtrn = (UWORD)0xFFFF;
    rp->PenWidth = 1;
    rp->PenHeight = 1;
    // Font stays nullptr until L4.5 binds GfxBase->DefaultFont.
}

// SetRast(rp, pen) — fill the RastPort's whole BitMap with `pen`.
void Croi_Gfx_SetRast_Impl(struct RastPort *rp, ULONG pen)
{
    if (!rp || !rp->BitMap) {
        return;
    }
    struct DathFramebuffer *fb = surf_of(rp->BitMap);
    if (!fb) {
        return;
    }
    Dath_Clear(fb, pen_to_color(pen, fb));
}
