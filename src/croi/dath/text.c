// SPDX-License-Identifier: BSD-2-Clause
//
// Bitmap text rendering. One row of a glyph fits in a single byte
// (Tier 1 only supports widths up to 8 pixels), which makes the inner
// loop a tight check-bit-and-pixel.

#include <cara/dath.h>
#include <cara/types.h>

void Dath_DrawChar(const struct DathFramebuffer *fb,
                   const struct DathFont *font,
                   i32 x, i32 y, char c, DathColor fg, DathColor bg)
{
    if (!fb || !font || !font->bitmap) {
        return;
    }
    u32 code = (u32)(u8)c;
    if (code < font->first_glyph || code > font->last_glyph) {
        Dath_FillRect(fb, x, y, (i32)font->width, (i32)font->height, bg);
        return;
    }
    const u8 *rows = font->bitmap + (code - font->first_glyph) * font->height;
    for (u32 yy = 0; yy < font->height; yy++) {
        u8 row = rows[yy];
        for (u32 xx = 0; xx < font->width; xx++) {
            bool set = (row >> (7 - xx)) & 1u;
            Dath_Pixel(fb, x + (i32)xx, y + (i32)yy, set ? fg : bg);
        }
    }
}

void Dath_DrawString(const struct DathFramebuffer *fb,
                     const struct DathFont *font,
                     i32 x, i32 y, const char *s, DathColor fg, DathColor bg)
{
    if (!fb || !font || !s) {
        return;
    }
    i32 cur_x = x;
    while (*s) {
        Dath_DrawChar(fb, font, cur_x, y, *s, fg, bg);
        cur_x += (i32)font->width;
        s++;
    }
}
