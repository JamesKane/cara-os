// SPDX-License-Identifier: BSD-2-Clause
//
// Off-screen DathFramebuffer surfaces — the AmigaOS BitMap analogue.
// Backing memory comes from the kernel heap (Croi_Alloc), so this file
// only builds for CARA_TARGET=riscv64. Host tests construct surfaces
// directly from static buffers via Dath_Framebuffer_Init.

#include <cara/alloc.h>
#include <cara/dath.h>
#include <cara/types.h>

static u32 bpp_of(DathFormat fmt)
{
    switch (fmt) {
    case DATH_FMT_RGBA8888:
    case DATH_FMT_BGRA8888:
        return 4;
    case DATH_FMT_RGB565:
        return 2;
    case DATH_FMT_NONE:
    default:
        return 0;
    }
}

[[nodiscard]] int Dath_AllocBitmap(struct DathFramebuffer *out, u32 width, u32 height,
                                   DathFormat format)
{
    if (!out || width == 0 || height == 0) {
        return CARA_EINVAL;
    }
    u32 bpp = bpp_of(format);
    if (bpp == 0) {
        return CARA_EINVAL;
    }
    usize size = (usize)width * (usize)height * bpp;
    if (size == 0) {
        return CARA_EINVAL;
    }
    void *base = Croi_Alloc(size);
    if (!base) {
        return CARA_ENOMEM;
    }
    int rc = Dath_Framebuffer_Init(out, base, width, height, width * bpp, format);
    if (rc != CARA_EOK) {
        Croi_Free(base);
        return rc;
    }
    return CARA_EOK;
}

void Dath_FreeBitmap(struct DathFramebuffer *fb)
{
    if (!fb) {
        return;
    }
    if (fb->base) {
        Croi_Free(fb->base);
    }
    *fb = (struct DathFramebuffer){ 0 };
}
