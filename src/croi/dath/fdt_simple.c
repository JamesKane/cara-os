// SPDX-License-Identifier: BSD-2-Clause
//
// Parse a `simple-framebuffer` FDT node — the convention U-Boot uses
// when it leaves a working framebuffer set up for the kernel to
// inherit. Documented in linux Documentation/devicetree/bindings/
// display/simple-framebuffer.yaml. We support the formats that show
// up in practice on the targets we care about: a8r8g8b8 / x8r8g8b8
// (32-bit) and r5g6b5 (16-bit). Anything else returns CARA_EINVAL so
// callers don't render garbage to a misinterpreted buffer.

#include <cara/dath.h>
#include <cara/fdt.h>
#include <cara/types.h>

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int parse_format(const char *s, DathFormat *out)
{
    if (!s) {
        return CARA_EINVAL;
    }
    if (str_eq(s, "a8r8g8b8") || str_eq(s, "x8r8g8b8")) {
        *out = DATH_FMT_RGBA8888;
        return CARA_EOK;
    }
    if (str_eq(s, "a8b8g8r8") || str_eq(s, "x8b8g8r8")) {
        *out = DATH_FMT_BGRA8888;
        return CARA_EOK;
    }
    if (str_eq(s, "r5g6b5")) {
        *out = DATH_FMT_RGB565;
        return CARA_EOK;
    }
    return CARA_EINVAL;
}

[[nodiscard]] int Dath_Framebuffer_FromFdt(struct DathFbDescriptor *out, const struct Fdt *fdt)
{
    if (!out || !fdt) {
        return CARA_EINVAL;
    }
    *out = (struct DathFbDescriptor){ 0 };

    u32 node = 0;
    int rc = Fdt_FindByCompatible(fdt, "simple-framebuffer", &node);
    if (rc != CARA_EOK) {
        return CARA_ENOTFOUND;
    }

    u64 base = 0;
    u64 size = 0;
    rc = Fdt_PropReg(fdt, node, 0, &base, &size);
    if (rc != CARA_EOK) {
        return rc;
    }

    u32 width = 0, height = 0, stride = 0;
    if (Fdt_PropU32(fdt, node, "width", &width) != CARA_EOK) {
        return CARA_EINVAL;
    }
    if (Fdt_PropU32(fdt, node, "height", &height) != CARA_EOK) {
        return CARA_EINVAL;
    }
    if (Fdt_PropU32(fdt, node, "stride", &stride) != CARA_EOK) {
        return CARA_EINVAL;
    }

    const char *fmt_str = Fdt_PropStr(fdt, node, "format");
    DathFormat fmt = DATH_FMT_NONE;
    rc = parse_format(fmt_str, &fmt);
    if (rc != CARA_EOK) {
        return rc;
    }

    out->phys_base = base;
    out->phys_size = size;
    out->width = width;
    out->height = height;
    out->stride = stride;
    out->format = fmt;
    return CARA_EOK;
}
