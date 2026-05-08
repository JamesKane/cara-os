// SPDX-License-Identifier: BSD-2-Clause
//
// dath drawing primitives — pixel set, filled rect, clear, and rect
// blit. RGBA8888 and BGRA8888 are handled identically (caller is
// responsible for handing in a DathColor that matches the format).
// RGB565 is included in the API surface but its encoding helper is
// stubbed until a 16-bit framebuffer actually appears.
//
// Negative origins and oversize spans are tolerated: clipping is
// done up-front so callers don't have to worry about partial visibility.

#include <cara/dath.h>
#include <cara/types.h>

DathColor Dath_RGB(u8 r, u8 g, u8 b)
{
    return ((u32)0xFF << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

DathColor Dath_RGBA(u8 r, u8 g, u8 b, u8 a)
{
    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

static inline u32 *pixel_addr_32(const struct DathFramebuffer *fb, i32 x, i32 y)
{
    u8 *row = (u8 *)fb->base + (usize)(u32)y * fb->stride;
    return (u32 *)(row + (usize)(u32)x * 4);
}

static i32 imin_i32(i32 a, i32 b)
{
    return a < b ? a : b;
}

static i32 imax_i32(i32 a, i32 b)
{
    return a > b ? a : b;
}

void Dath_Pixel(const struct DathFramebuffer *fb, i32 x, i32 y, DathColor c)
{
    if (!fb || !fb->base) {
        return;
    }
    if (x < 0 || y < 0 || (u32)x >= fb->width || (u32)y >= fb->height) {
        return;
    }
    if (fb->bpp == 4) {
        *pixel_addr_32(fb, x, y) = c;
    }
    // 16-bit path lands when an RGB565 framebuffer actually appears.
}

void Dath_FillRect(const struct DathFramebuffer *fb, i32 x, i32 y,
                   i32 w, i32 h, DathColor c)
{
    if (!fb || !fb->base || w <= 0 || h <= 0) {
        return;
    }
    i32 x0 = imax_i32(x, 0);
    i32 y0 = imax_i32(y, 0);
    i32 x1 = imin_i32(x + w, (i32)fb->width);
    i32 y1 = imin_i32(y + h, (i32)fb->height);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    if (fb->bpp == 4) {
        for (i32 yy = y0; yy < y1; yy++) {
            u32 *p = pixel_addr_32(fb, x0, yy);
            i32 row_w = x1 - x0;
            for (i32 i = 0; i < row_w; i++) {
                p[i] = c;
            }
        }
    }
}

void Dath_Clear(const struct DathFramebuffer *fb, DathColor c)
{
    if (!fb) {
        return;
    }
    Dath_FillRect(fb, 0, 0, (i32)fb->width, (i32)fb->height, c);
}

void Dath_BlitRect(const struct DathFramebuffer *dst, i32 dx, i32 dy,
                   const struct DathFramebuffer *src, i32 sx, i32 sy,
                   i32 w, i32 h)
{
    if (!dst || !src || !dst->base || !src->base || w <= 0 || h <= 0) {
        return;
    }
    if (dst->format != src->format) {
        return;
    }

    // Clip source bounds.
    i32 sx0 = imax_i32(sx, 0);
    i32 sy0 = imax_i32(sy, 0);
    i32 sx1 = imin_i32(sx + w, (i32)src->width);
    i32 sy1 = imin_i32(sy + h, (i32)src->height);

    // Move destination origin by however much we trimmed the source.
    i32 dx0 = dx + (sx0 - sx);
    i32 dy0 = dy + (sy0 - sy);

    // Clip against destination on the low side.
    if (dx0 < 0) {
        sx0 -= dx0;
        dx0 = 0;
    }
    if (dy0 < 0) {
        sy0 -= dy0;
        dy0 = 0;
    }

    i32 cw = sx1 - sx0;
    i32 ch = sy1 - sy0;
    if ((i32)dst->width - dx0 < cw) {
        cw = (i32)dst->width - dx0;
    }
    if ((i32)dst->height - dy0 < ch) {
        ch = (i32)dst->height - dy0;
    }
    if (cw <= 0 || ch <= 0) {
        return;
    }

    if (dst->bpp == 4) {
        for (i32 r = 0; r < ch; r++) {
            const u32 *sp = (const u32 *)((const u8 *)src->base
                                          + (usize)(u32)(sy0 + r) * src->stride
                                          + (usize)(u32)sx0 * 4);
            u32       *dp = (u32 *)((u8 *)dst->base
                                    + (usize)(u32)(dy0 + r) * dst->stride
                                    + (usize)(u32)dx0 * 4);
            for (i32 i = 0; i < cw; i++) {
                dp[i] = sp[i];
            }
        }
    }
}
