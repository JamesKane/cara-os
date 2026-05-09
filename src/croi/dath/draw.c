// SPDX-License-Identifier: BSD-2-Clause
//
// dath drawing primitives — pixel set, filled rect, clear, and rect
// blit. RGBA8888 and BGRA8888 are handled identically (caller is
// responsible for handing in a DathColor that matches the format).
// RGB565 packs the colour into the low 16 bits of the DathColor and
// the writes go through 16-bit stores; the high half is ignored.
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

DathColor Dath_RGB565(u8 r, u8 g, u8 b)
{
    // 5+6+5 packed into a u16; takes the high bits of each channel.
    return (((u32)r & 0xF8) << 8) | (((u32)g & 0xFC) << 3) | ((u32)b >> 3);
}

static inline u32 *pixel_addr_32(const struct DathFramebuffer *fb, i32 x, i32 y)
{
    u8 *row = (u8 *)fb->base + (usize)(u32)y * fb->stride;
    return (u32 *)(row + (usize)(u32)x * 4);
}

static inline u16 *pixel_addr_16(const struct DathFramebuffer *fb, i32 x, i32 y)
{
    u8 *row = (u8 *)fb->base + (usize)(u32)y * fb->stride;
    return (u16 *)(row + (usize)(u32)x * 2);
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
    } else if (fb->bpp == 2) {
        *pixel_addr_16(fb, x, y) = (u16)c;
    }
}

void Dath_FillRect(const struct DathFramebuffer *fb, i32 x, i32 y, i32 w, i32 h, DathColor c)
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

    i32 row_w = x1 - x0;
    if (fb->bpp == 4) {
        for (i32 yy = y0; yy < y1; yy++) {
            u32 *p = pixel_addr_32(fb, x0, yy);
            for (i32 i = 0; i < row_w; i++) {
                p[i] = c;
            }
        }
    } else if (fb->bpp == 2) {
        u16 v = (u16)c;
        for (i32 yy = y0; yy < y1; yy++) {
            u16 *p = pixel_addr_16(fb, x0, yy);
            for (i32 i = 0; i < row_w; i++) {
                p[i] = v;
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
                   const struct DathFramebuffer *src, i32 sx, i32 sy, i32 w, i32 h)
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
            const u32 *sp = (const u32 *)((const u8 *)src->base +
                                          (usize)(u32)(sy0 + r) * src->stride +
                                          (usize)(u32)sx0 * 4);
            u32 *dp = (u32 *)((u8 *)dst->base + (usize)(u32)(dy0 + r) * dst->stride +
                              (usize)(u32)dx0 * 4);
            for (i32 i = 0; i < cw; i++) {
                dp[i] = sp[i];
            }
        }
    } else if (dst->bpp == 2) {
        for (i32 r = 0; r < ch; r++) {
            const u16 *sp = (const u16 *)((const u8 *)src->base +
                                          (usize)(u32)(sy0 + r) * src->stride +
                                          (usize)(u32)sx0 * 2);
            u16 *dp = (u16 *)((u8 *)dst->base + (usize)(u32)(dy0 + r) * dst->stride +
                              (usize)(u32)dx0 * 2);
            for (i32 i = 0; i < cw; i++) {
                dp[i] = sp[i];
            }
        }
    }
}

// Bresenham line. Tracks absolute deltas, signs both axes, accumulates
// a 2*err threshold to decide when to step the minor axis. Off-screen
// pixels are silently dropped by Dath_Pixel's bounds check.
void Dath_DrawLine(const struct DathFramebuffer *fb, i32 x0, i32 y0, i32 x1, i32 y1, DathColor c)
{
    if (!fb || !fb->base) {
        return;
    }
    i32 dx = x1 - x0;
    if (dx < 0) {
        dx = -dx;
    }
    i32 dy = y1 - y0;
    if (dy < 0) {
        dy = -dy;
    }
    i32 sx = (x0 < x1) ? 1 : -1;
    i32 sy = (y0 < y1) ? 1 : -1;
    i32 err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        Dath_Pixel(fb, x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        i32 e2 = err;
        if (e2 > -dx) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dy) {
            err += dx;
            y0 += sy;
        }
    }
}

// Outlined rectangle: four edges via DrawLine. Endpoints are inclusive
// so each corner pixel is written exactly once.
void Dath_DrawRect(const struct DathFramebuffer *fb, i32 x, i32 y, i32 w, i32 h, DathColor c)
{
    if (!fb || w <= 0 || h <= 0) {
        return;
    }
    i32 x1 = x + w - 1;
    i32 y1 = y + h - 1;
    Dath_DrawLine(fb, x, y, x1, y, c);   // top
    Dath_DrawLine(fb, x, y1, x1, y1, c); // bottom
    Dath_DrawLine(fb, x, y, x, y1, c);   // left
    Dath_DrawLine(fb, x1, y, x1, y1, c); // right
}
