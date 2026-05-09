// SPDX-License-Identifier: BSD-2-Clause
//
// Default Leargas pointer — a 16×16 arrow with a black outline and
// white fill, hot-spot at the tip (0, 0). Same encoding pattern as
// dath_font_8x8: fixed-size pixel grid laid out at compile time.
// Phase 3+ adds per-screen / per-window cursors via the V36+
// Pointer / SetPointer API on top of this.

#include <cara/leargas.h>
#include <cara/types.h>

#define _ LEARGAS_PTR_TRANSPARENT
#define W LEARGAS_PTR_FG
#define B LEARGAS_PTR_BG

// Hand-rolled arrow. The diagonal edge is the outline; the interior
// is filled. Each row is 16 columns. Top-left is the tip; the tail
// curls down-right with a heel that swings back to give a recognisable
// arrow silhouette rather than a plain triangle.
static const u8 arrow_pixels[16 * 16] = {
    // clang-format off
    B, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _,
    B, B, _, _, _, _, _, _, _, _, _, _, _, _, _, _,
    B, W, B, _, _, _, _, _, _, _, _, _, _, _, _, _,
    B, W, W, B, _, _, _, _, _, _, _, _, _, _, _, _,
    B, W, W, W, B, _, _, _, _, _, _, _, _, _, _, _,
    B, W, W, W, W, B, _, _, _, _, _, _, _, _, _, _,
    B, W, W, W, W, W, B, _, _, _, _, _, _, _, _, _,
    B, W, W, W, W, W, W, B, _, _, _, _, _, _, _, _,
    B, W, W, W, W, W, W, W, B, _, _, _, _, _, _, _,
    B, W, W, W, W, W, W, W, W, B, _, _, _, _, _, _,
    B, W, W, W, W, W, B, B, B, B, B, _, _, _, _, _,
    B, W, W, B, W, W, B, _, _, _, _, _, _, _, _, _,
    B, W, B, _, B, W, W, B, _, _, _, _, _, _, _, _,
    B, B, _, _, B, W, W, B, _, _, _, _, _, _, _, _,
    B, _, _, _, _, B, W, W, B, _, _, _, _, _, _, _,
    _, _, _, _, _, _, B, B, _, _, _, _, _, _, _, _,
    // clang-format on
};

#undef _
#undef W
#undef B

const struct LeargasPointerImage leargas_pointer_arrow = {
    .width = 16,
    .height = 16,
    .hot_x = 0,
    .hot_y = 0,
    .pixels = arrow_pixels,
};
