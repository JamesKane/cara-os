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
#include <graphics/text.h>

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

// The RastPort's current foreground colour for the target surface.
static DathColor fg_color(const struct RastPort *rp, const struct DathFramebuffer *fb)
{
    return pen_to_color((ULONG)(UBYTE)rp->FgPen, fb);
}

// SetAPen/SetBPen/SetDrMd — pure RastPort state (no Dath call). v0
// honours FgPen for primitives; JAM1/JAM2 draw identically (BgPen is used
// only by text later); COMPLEMENT is not implemented (§6).
void Croi_Gfx_SetAPen_Impl(struct RastPort *rp, ULONG pen)
{
    if (rp) {
        rp->FgPen = (BYTE)pen;
    }
}

void Croi_Gfx_SetBPen_Impl(struct RastPort *rp, ULONG pen)
{
    if (rp) {
        rp->BgPen = (BYTE)pen;
    }
}

void Croi_Gfx_SetDrMd_Impl(struct RastPort *rp, ULONG mode)
{
    if (rp) {
        rp->DrawMode = (BYTE)mode;
    }
}

// Move(rp, x, y) — set the graphics cursor (no draw).
void Croi_Gfx_Move_Impl(struct RastPort *rp, WORD x, WORD y)
{
    if (rp) {
        rp->cp_x = x;
        rp->cp_y = y;
    }
}

// Draw(rp, x, y) — line from the cursor to (x,y) in FgPen; cursor moves.
void Croi_Gfx_Draw_Impl(struct RastPort *rp, WORD x, WORD y)
{
    if (!rp || !rp->BitMap) {
        return;
    }
    struct DathFramebuffer *fb = surf_of(rp->BitMap);
    if (!fb) {
        return;
    }
    Dath_DrawLine(fb, rp->cp_x, rp->cp_y, x, y, fg_color(rp, fb));
    rp->cp_x = x;
    rp->cp_y = y;
}

// RectFill(rp, xMin, yMin, xMax, yMax) — filled rect, inclusive corners,
// in FgPen.
void Croi_Gfx_RectFill_Impl(struct RastPort *rp, WORD xMin, WORD yMin, WORD xMax, WORD yMax)
{
    if (!rp || !rp->BitMap) {
        return;
    }
    struct DathFramebuffer *fb = surf_of(rp->BitMap);
    if (!fb) {
        return;
    }
    Dath_FillRect(fb, xMin, yMin, (i32)xMax - xMin + 1, (i32)yMax - yMin + 1, fg_color(rp, fb));
}

// WritePixel(rp, x, y) — set (x,y) to FgPen. Returns 0 on success, -1 if
// (x,y) is outside the BitMap.
LONG Croi_Gfx_WritePixel_Impl(struct RastPort *rp, WORD x, WORD y)
{
    if (!rp || !rp->BitMap) {
        return -1;
    }
    struct DathFramebuffer *fb = surf_of(rp->BitMap);
    if (!fb || x < 0 || y < 0 || (u32)x >= fb->width || (u32)y >= fb->height) {
        return -1;
    }
    Dath_Pixel(fb, x, y, fg_color(rp, fb));
    return 0;
}

// ReadPixel(rp, x, y) — return the raw chunky pixel value at (x,y) (v0:
// no reverse-palette lookup; §6.2), or -1 if (x,y) is outside.
LONG Croi_Gfx_ReadPixel_Impl(struct RastPort *rp, WORD x, WORD y)
{
    if (!rp || !rp->BitMap) {
        return -1;
    }
    struct DathFramebuffer *fb = surf_of(rp->BitMap);
    if (!fb || !fb->base || x < 0 || y < 0 || (u32)x >= fb->width || (u32)y >= fb->height) {
        return -1;
    }
    const u8 *row = (const u8 *)fb->base + (usize)(u32)y * fb->stride;
    if (fb->bpp == 4) {
        u32 v = *(const u32 *)(row + (usize)(u32)x * 4);
        return (LONG)v;
    }
    u16 v = *(const u16 *)(row + (usize)(u32)x * 2);
    return (LONG)v;
}

// Shared blit kernel impl for BltBitMap / BltBitMapRastPort / ClipBlit
// (the U-mode stubs in graphics_blit.c resolve operands to src/dest
// BitMaps and pack GfxBltArgs). Dath_BlitRect copies the rect between the
// chunky surfaces and clips both sides; it no-ops on a format mismatch
// (a v0 gap — §6.4). Returns 0 (V36 BltBitMap's plane count is moot for
// chunky surfaces).
LONG Croi_Gfx_Blt_Impl(const struct GfxBltArgs *a)
{
    if (!a || !a->src || !a->dest) {
        return 0;
    }
    struct DathFramebuffer *s = surf_of((struct BitMap *)a->src);
    struct DathFramebuffer *d = surf_of(a->dest);
    if (!s || !d) {
        return 0;
    }
    Dath_BlitRect(d, a->xDest, a->yDest, s, a->xSrc, a->ySrc, a->xSize, a->ySize);
    return 0;
}

// The single v0 system font — a TextFont describing dath_font_8x8, lazily
// allocated in the shared heap (U-mode-readable). graphics.library has
// one face in v0, so OpenFont returns this regardless of the TextAttr and
// the rasteriser always renders from dath_font_8x8 (§5).
static struct TextFont *g_system_font = nullptr;

static struct TextFont *gfx_system_font(void)
{
    if (!g_system_font) {
        struct TextFont *tf = (struct TextFont *)Croi_AllocShared(sizeof(*tf));
        if (!tf) {
            return nullptr;
        }
        *tf = (struct TextFont){ 0 };
        tf->tf_YSize = (UWORD)dath_font_8x8.height;
        tf->tf_XSize = (UWORD)dath_font_8x8.width;
        tf->tf_Baseline = (UWORD)(dath_font_8x8.height - 1);
        tf->tf_LoChar = (UBYTE)dath_font_8x8.first_glyph;
        tf->tf_HiChar = (UBYTE)dath_font_8x8.last_glyph;
        tf->tf_Style = FS_NORMAL;
        tf->tf_Flags = FPF_ROMFONT;
        tf->tf_Accessors = 1;
        g_system_font = tf;
    }
    return g_system_font;
}

// OpenFont(textAttr) — v0 returns the single system font regardless of
// the requested attributes (one face until diskfont/more ROM fonts).
struct TextFont *Croi_Gfx_OpenFont_Impl(struct TextAttr *textAttr)
{
    (void)textAttr;
    return gfx_system_font();
}

// CloseFont — no-op; the system font persists for the session.
void Croi_Gfx_CloseFont_Impl(struct TextFont *textFont)
{
    (void)textFont;
}

// SetFont(rp, tf) — bind the RastPort's font + cache its metrics.
void Croi_Gfx_SetFont_Impl(struct RastPort *rp, struct TextFont *textFont)
{
    if (!rp || !textFont) {
        return;
    }
    rp->Font = textFont;
    rp->TxHeight = textFont->tf_YSize;
    rp->TxWidth = textFont->tf_XSize;
    rp->TxBaseline = textFont->tf_Baseline;
}

// TextLength(rp, string, count) — pixel width of `count` chars. Monospace
// v0: count * the system font's width (rp->Font is the same single face).
LONG Croi_Gfx_TextLength_Impl(struct RastPort *rp, STRPTR string, ULONG count)
{
    (void)rp;
    (void)string;
    return (LONG)(count * dath_font_8x8.width);
}

// Text(rp, string, count) — render `count` chars from the graphics cursor
// in FgPen over BgPen (Dath_DrawChar), advancing the cursor. v0 treats
// cp_y as the glyph TOP (not the AmigaDOS baseline) so cp_y=0 is visible
// and it matches Leargas chrome rendering (§5).
void Croi_Gfx_Text_Impl(struct RastPort *rp, STRPTR string, ULONG count)
{
    if (!rp || !rp->BitMap || !string) {
        return;
    }
    struct DathFramebuffer *fb = surf_of(rp->BitMap);
    if (!fb) {
        return;
    }
    DathColor fg = fg_color(rp, fb);
    DathColor bg = pen_to_color((ULONG)(UBYTE)rp->BgPen, fb);
    i32 x = rp->cp_x;
    i32 y = rp->cp_y;
    for (ULONG i = 0; i < count; i++) {
        Dath_DrawChar(fb, &dath_font_8x8, x, y, (char)string[i], fg, bg);
        x += (i32)dath_font_8x8.width;
    }
    rp->cp_x = (WORD)x;
}
