// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LA — pointer rendering. Save / restore via Dath_BlitRect
// against a caller-provided same-format off-screen DathFramebuffer;
// composite via per-pixel Dath_Pixel walking the LeargasPointerImage's
// ternary mask.
//
// Why caller-provided save: Dath_AllocBitmap is kernel-only (uses
// Croi_Alloc), and we want the host build to exercise this code in
// unit tests. Kernel callers allocate via Dath_AllocBitmap and pass
// the descriptor in; tests stack-mount a synthetic DathFramebuffer
// backed by a u8[] of the right size.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>

// Save the screen pixels at the pointer's current top-left corner
// into the save buffer. Dath_BlitRect already independently clips
// src and dst, so partial off-screen pointer regions still produce
// a correct save (the corresponding restore re-blits only the
// originally-visible pixels).
static void save_underneath(struct LeargasPointer *p, i32 top_x, i32 top_y)
{
    Dath_BlitRect(p->save, 0, 0, p->fb, top_x, top_y, (i32)p->img->width, (i32)p->img->height);
}

// Restore the previously-saved screen pixels at `top_x, top_y`.
static void restore_underneath(struct LeargasPointer *p, i32 top_x, i32 top_y)
{
    Dath_BlitRect(p->fb, top_x, top_y, p->save, 0, 0, (i32)p->img->width, (i32)p->img->height);
}

// Composite the pointer image at `top_x, top_y` onto the framebuffer.
// Transparent pixels are skipped; fg/bg pixels write through Dath_Pixel
// which clips to the framebuffer extent.
static void composite_pointer(struct LeargasPointer *p, i32 top_x, i32 top_y)
{
    const u8 *px = p->img->pixels;
    u32 w = p->img->width;
    u32 h = p->img->height;
    for (u32 yy = 0; yy < h; yy++) {
        for (u32 xx = 0; xx < w; xx++) {
            u8 v = px[yy * w + xx];
            if (v == LEARGAS_PTR_TRANSPARENT) {
                continue;
            }
            DathColor c = (v == LEARGAS_PTR_FG) ? p->fg : p->bg;
            Dath_Pixel(p->fb, top_x + (i32)xx, top_y + (i32)yy, c);
        }
    }
}

[[nodiscard]] int Leargas_Pointer_Init(struct LeargasPointer *p, struct DathFramebuffer *fb,
                                       struct DathFramebuffer *save,
                                       const struct LeargasPointerImage *img, DathColor fg,
                                       DathColor bg, i32 x0, i32 y0)
{
    if (!p || !fb || !save || !img || !img->pixels) {
        return CARA_EINVAL;
    }
    if (img->width == 0 || img->height == 0) {
        return CARA_EINVAL;
    }
    if (fb->format != save->format || fb->bpp != save->bpp) {
        return CARA_EINVAL;
    }
    if (save->width < img->width || save->height < img->height) {
        return CARA_EINVAL;
    }

    p->fb = fb;
    p->save = save;
    p->img = img;
    p->fg = fg;
    p->bg = bg;
    p->x = x0;
    p->y = y0;
    p->save_valid = false;

    i32 top_x = x0 - img->hot_x;
    i32 top_y = y0 - img->hot_y;
    save_underneath(p, top_x, top_y);
    composite_pointer(p, top_x, top_y);
    p->save_valid = true;
    return CARA_EOK;
}

void Leargas_Pointer_Move(struct LeargasPointer *p, i32 x, i32 y)
{
    if (!p || !p->fb || !p->save || !p->img) {
        return;
    }
    if (x == p->x && y == p->y) {
        return;
    }

    i32 old_top_x = p->x - p->img->hot_x;
    i32 old_top_y = p->y - p->img->hot_y;
    i32 new_top_x = x - p->img->hot_x;
    i32 new_top_y = y - p->img->hot_y;

    // Order matters when old / new regions overlap: restoring first
    // wipes any pointer pixels in the overlap region, then the save
    // captures the now-correct underneath, then composite paints.
    if (p->save_valid) {
        restore_underneath(p, old_top_x, old_top_y);
    }
    save_underneath(p, new_top_x, new_top_y);
    composite_pointer(p, new_top_x, new_top_y);

    p->x = x;
    p->y = y;
    p->save_valid = true;
}
