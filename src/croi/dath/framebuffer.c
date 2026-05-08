// SPDX-License-Identifier: BSD-2-Clause
//
// DathFramebuffer setup. Phase 1 only takes parameters as arguments;
// FDT-driven discovery of /chosen/framebuffer comes once we have a
// QEMU/board with a simple-framebuffer node to discover.

#include <cara/dath.h>
#include <cara/types.h>

static u32 bpp_from_format(DathFormat fmt)
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

[[nodiscard]] int Dath_Framebuffer_Init(struct DathFramebuffer *fb, void *base,
                                        u32 width, u32 height, u32 stride,
                                        DathFormat format)
{
    if (!fb || !base || width == 0 || height == 0) {
        return CARA_EINVAL;
    }
    u32 bpp = bpp_from_format(format);
    if (bpp == 0) {
        return CARA_EINVAL;
    }
    if (stride < width * bpp) {
        return CARA_EINVAL;
    }
    fb->base = base;
    fb->width = width;
    fb->height = height;
    fb->stride = stride;
    fb->format = format;
    fb->bpp = bpp;
    return CARA_EOK;
}
