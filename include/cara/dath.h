// SPDX-License-Identifier: BSD-2-Clause
//
// dath.library core — framebuffer abstraction and basic drawing
// primitives. Phase 1 Subgoal 4 inherits whatever firmware sets up
// via a `simple-framebuffer` FDT node (the X1's U-Boot does this on
// HDMI bring-up). A from-scratch DPU driver is Phase 4 work.
//
// In Cara nomenclature, "dath" is Irish for colour — the AmigaOS
// graphics.library analogue. The eventual API surface (Phase 3) will
// match the RKM Libraries shape — RastPort, BitMap, BltCon — but we
// stage that on top of these primitives once a real display is up.

#ifndef CARA_DATH_H
#define CARA_DATH_H

#include <cara/types.h>

typedef enum : u32 {
    DATH_FMT_NONE     = 0,
    DATH_FMT_RGBA8888 = 1,    // 0xAARRGGBB layout in u32 LE memory
    DATH_FMT_BGRA8888 = 2,    // 0xAABBGGRR layout in u32 LE memory
    DATH_FMT_RGB565   = 3,    // 16-bit packed
} DathFormat;

struct DathFramebuffer {
    void      *base;          // upper-half kernel VA pointer to pixel(0,0)
    u32        width;         // pixels
    u32        height;
    u32        stride;        // bytes per row (>= width * bpp)
    DathFormat format;
    u32        bpp;           // bytes per pixel — derived from format
};

// Encoded color value matching the framebuffer's format. Helpers below
// compose from 8-bit-per-channel RGB(A); the value can be passed
// directly to Dath_Pixel / Dath_FillRect / Dath_Clear.
typedef u32 DathColor;

// Initialise an existing DathFramebuffer struct from explicit
// parameters. base is an upper-half kernel VA pointing at pixel (0, 0);
// stride is bytes per row; format determines bpp.
[[nodiscard]] int Dath_Framebuffer_Init(struct DathFramebuffer *fb, void *base,
                                        u32 width, u32 height, u32 stride,
                                        DathFormat format);

// Compose colors. RGBA8888 / BGRA8888 framebuffers store the alpha in
// the high byte; RGB565 ignores alpha. Dath_RGB defaults alpha to 0xFF.
DathColor Dath_RGB(u8 r, u8 g, u8 b);
DathColor Dath_RGBA(u8 r, u8 g, u8 b, u8 a);

void Dath_Pixel(const struct DathFramebuffer *fb, i32 x, i32 y, DathColor c);

// Filled rectangle, clipped to the framebuffer bounds. Negative
// origins or oversize widths/heights are handled by clipping.
void Dath_FillRect(const struct DathFramebuffer *fb, i32 x, i32 y,
                   i32 w, i32 h, DathColor c);

// Convenience: fill the whole framebuffer with `c`.
void Dath_Clear(const struct DathFramebuffer *fb, DathColor c);

// Copy a w×h region from `src` at (sx, sy) to `dst` at (dx, dy).
// Both framebuffers must have the same format. Source and destination
// regions are clipped independently to their respective bounds.
void Dath_BlitRect(const struct DathFramebuffer *dst, i32 dx, i32 dy,
                   const struct DathFramebuffer *src, i32 sx, i32 sy,
                   i32 w, i32 h);

#endif
