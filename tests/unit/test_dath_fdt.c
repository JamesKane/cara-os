// SPDX-License-Identifier: BSD-2-Clause
//
// Host unit test for Dath_Framebuffer_FromFdt. Hand-builds a tiny FDT
// with a /chosen/framebuffer node carrying the simple-framebuffer
// binding's properties, then verifies the parser extracts base, size,
// width, height, stride, and format correctly. Also exercises the
// no-such-node and unsupported-format paths.

#include <cara/dath.h>
#include <cara/fdt.h>
#include <cara/types.h>

#include <stdio.h>
#include <string.h>

#define FDT_MAGIC 0xD00DFEEDu
#define BEGIN_NODE 0x00000001u
#define END_NODE 0x00000002u
#define PROP 0x00000003u
#define FDT_END 0x00000009u

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_dath_fdt: FAIL: %s\n", msg);
    return code;
}

static void put_be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static void emit_token(u8 *buf, u32 *poff, u32 tok)
{
    put_be32(buf + *poff, tok);
    *poff += 4;
}

static void emit_begin_node(u8 *buf, u32 *poff, const char *name)
{
    emit_token(buf, poff, BEGIN_NODE);
    u32 nlen = (u32)strlen(name) + 1;
    memcpy(buf + *poff, name, nlen);
    *poff += nlen;
    while ((*poff & 3u) != 0) {
        buf[(*poff)++] = 0;
    }
}

static void emit_end_node(u8 *buf, u32 *poff)
{
    emit_token(buf, poff, END_NODE);
}

static void emit_prop(u8 *buf, u32 *poff, u32 nameoff, const u8 *data, u32 len)
{
    emit_token(buf, poff, PROP);
    put_be32(buf + *poff, len);
    *poff += 4;
    put_be32(buf + *poff, nameoff);
    *poff += 4;
    memcpy(buf + *poff, data, len);
    *poff += len;
    while ((*poff & 3u) != 0) {
        buf[(*poff)++] = 0;
    }
}

static u32 string_intern(u8 *strings, u32 *plen, const char *s)
{
    u32 off = *plen;
    u32 n = (u32)strlen(s) + 1;
    memcpy(strings + off, s, n);
    *plen += n;
    return off;
}

// Build a minimal FDT:
//   / {
//       #address-cells = <2>;
//       #size-cells = <2>;
//       framebuffer {
//           compatible = "simple-framebuffer";
//           reg = <0x0 0x80800000 0x0 0x300000>;   // 2 + 2 cells
//           width = <800>;
//           height = <600>;
//           stride = <(800 * 4)>;
//           format = "a8r8g8b8";
//       };
//   };
//
// The framebuffer lives directly under root so root's 2/2 cells
// govern the reg decoding (rather than relying on /chosen, which
// per spec defaults to #address-cells=2 / #size-cells=1 and would
// reject our 4-cell reg). Both placements are valid per the binding;
// the root-level form is also what Raspberry Pi DTBs use when the
// framebuffer is hoisted out of /chosen.
//
// If `omit_fb` is true, the framebuffer node is left out (used for
// the no-such-node test). If `bad_format` is true, we use "wat" as
// the format string.
static u32 build_fdt(u8 *final, u32 final_cap, bool omit_fb, bool bad_format)
{
    static u8 strings[256];
    u32 slen = 0;
    u32 s_acells = string_intern(strings, &slen, "#address-cells");
    u32 s_scells = string_intern(strings, &slen, "#size-cells");
    u32 s_compat = string_intern(strings, &slen, "compatible");
    u32 s_reg = string_intern(strings, &slen, "reg");
    u32 s_width = string_intern(strings, &slen, "width");
    u32 s_height = string_intern(strings, &slen, "height");
    u32 s_stride = string_intern(strings, &slen, "stride");
    u32 s_format = string_intern(strings, &slen, "format");

    static u8 sblk[1024];
    u32 soff = 0;
    emit_begin_node(sblk, &soff, "");
    {
        u8 v[4];
        put_be32(v, 2);
        emit_prop(sblk, &soff, s_acells, v, 4);
        put_be32(v, 2);
        emit_prop(sblk, &soff, s_scells, v, 4);

        if (!omit_fb) {
            emit_begin_node(sblk, &soff, "framebuffer");
            {
                const char *cc = "simple-framebuffer";
                emit_prop(sblk, &soff, s_compat, (const u8 *)cc, (u32)strlen(cc) + 1);
                // reg = <0x0 0x80800000 0x0 0x300000> (2 addr + 2 size cells)
                u8 reg_be[16];
                put_be32(reg_be + 0, 0);
                put_be32(reg_be + 4, 0x80800000u);
                put_be32(reg_be + 8, 0);
                put_be32(reg_be + 12, 0x300000u);
                emit_prop(sblk, &soff, s_reg, reg_be, 16);

                put_be32(v, 800);
                emit_prop(sblk, &soff, s_width, v, 4);
                put_be32(v, 600);
                emit_prop(sblk, &soff, s_height, v, 4);
                put_be32(v, 800 * 4);
                emit_prop(sblk, &soff, s_stride, v, 4);

                const char *fc = bad_format ? "wat" : "a8r8g8b8";
                emit_prop(sblk, &soff, s_format, (const u8 *)fc, (u32)strlen(fc) + 1);
            }
            emit_end_node(sblk, &soff);
        }
    }
    emit_end_node(sblk, &soff);
    emit_token(sblk, &soff, FDT_END);
    while ((soff & 3u) != 0) {
        sblk[soff++] = 0;
    }

    // Lay out: header (40) | rsvmap (16) | struct | strings.
    u32 hdr = 40;
    u32 rsvmap_off = hdr;
    u32 rsvmap_size = 16;
    u32 struct_off = rsvmap_off + rsvmap_size;
    u32 totalsize = struct_off + soff + slen;
    if (totalsize > final_cap) {
        fprintf(stderr, "test_dath_fdt: FDT too large for buffer\n");
        return 0;
    }
    memset(final, 0, totalsize);

    put_be32(final + 0, FDT_MAGIC);
    put_be32(final + 4, totalsize);
    put_be32(final + 8, struct_off);
    put_be32(final + 12, struct_off + soff);
    put_be32(final + 16, rsvmap_off);
    put_be32(final + 20, 17);
    put_be32(final + 24, 16);
    put_be32(final + 28, 0);
    put_be32(final + 32, slen);
    put_be32(final + 36, soff);

    memcpy(final + struct_off, sblk, soff);
    memcpy(final + struct_off + soff, strings, slen);
    return totalsize;
}

int main(void)
{
    static u8 blob[2048] __attribute__((aligned(8)));

    // 1. Well-formed framebuffer node.
    u32 sz = build_fdt(blob, sizeof(blob), /*omit_fb=*/false,
                       /*bad_format=*/false);
    if (sz == 0) {
        return fail("build fdt failed", 1);
    }

    struct Fdt fdt;
    if (Fdt_Open(&fdt, blob) != CARA_EOK) {
        return fail("Fdt_Open failed", 2);
    }

    struct DathFbDescriptor desc;
    int rc = Dath_Framebuffer_FromFdt(&desc, &fdt);
    if (rc != CARA_EOK) {
        return fail("FromFdt: well-formed parse failed", 3);
    }
    if (desc.phys_base != 0x80800000ull) {
        return fail("phys_base wrong", 4);
    }
    if (desc.phys_size != 0x300000ull) {
        return fail("phys_size wrong", 5);
    }
    if (desc.width != 800 || desc.height != 600 || desc.stride != 800 * 4) {
        return fail("dims/stride wrong", 6);
    }
    if (desc.format != DATH_FMT_RGBA8888) {
        return fail("format not a8r8g8b8 → RGBA8888", 7);
    }

    // 2. No framebuffer node.
    sz = build_fdt(blob, sizeof(blob), /*omit_fb=*/true, /*bad_format=*/false);
    if (sz == 0) {
        return fail("build fdt no-fb failed", 8);
    }
    if (Fdt_Open(&fdt, blob) != CARA_EOK) {
        return fail("Fdt_Open no-fb failed", 9);
    }
    rc = Dath_Framebuffer_FromFdt(&desc, &fdt);
    if (rc != CARA_ENOTFOUND) {
        return fail("missing fb node didn't return ENOTFOUND", 10);
    }

    // 3. Unsupported format string.
    sz = build_fdt(blob, sizeof(blob), /*omit_fb=*/false, /*bad_format=*/true);
    if (sz == 0) {
        return fail("build fdt bad-fmt failed", 11);
    }
    if (Fdt_Open(&fdt, blob) != CARA_EOK) {
        return fail("Fdt_Open bad-fmt failed", 12);
    }
    rc = Dath_Framebuffer_FromFdt(&desc, &fdt);
    if (rc != CARA_EINVAL) {
        return fail("unsupported format didn't return EINVAL", 13);
    }

    puts("dath fdt ok");
    return 0;
}
