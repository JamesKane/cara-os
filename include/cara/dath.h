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

#include <cara/fdt.h>
#include <cara/types.h>

typedef enum : u32 {
    DATH_FMT_NONE = 0,
    DATH_FMT_RGBA8888 = 1, // 0xAARRGGBB layout in u32 LE memory
    DATH_FMT_BGRA8888 = 2, // 0xAABBGGRR layout in u32 LE memory
    DATH_FMT_RGB565 = 3,   // 16-bit packed
} DathFormat;

struct DathFramebuffer {
    void *base; // upper-half kernel VA pointer to pixel(0,0)
    u32 width;  // pixels
    u32 height;
    u32 stride; // bytes per row (>= width * bpp)
    DathFormat format;
    u32 bpp; // bytes per pixel — derived from format
};

// Encoded color value matching the framebuffer's format. Helpers below
// compose from 8-bit-per-channel RGB(A); the value can be passed
// directly to Dath_Pixel / Dath_FillRect / Dath_Clear.
typedef u32 DathColor;

// Initialise an existing DathFramebuffer struct from explicit
// parameters. base is an upper-half kernel VA pointing at pixel (0, 0);
// stride is bytes per row; format determines bpp.
[[nodiscard]] int Dath_Framebuffer_Init(struct DathFramebuffer *fb, void *base, u32 width,
                                        u32 height, u32 stride, DathFormat format);

// Allocate an off-screen drawing surface (the AmigaOS BitMap analogue).
// Backing memory comes from the kernel heap; stride is set to the
// minimum (width * bpp) and the buffer is zeroed by Croi_Alloc. Pair
// with Dath_FreeBitmap. Kernel-only — not available in host builds
// since Croi_Alloc lives in the kernel heap module.
[[nodiscard]] int Dath_AllocBitmap(struct DathFramebuffer *out, u32 width, u32 height,
                                   DathFormat format);

// Release a bitmap previously allocated via Dath_AllocBitmap. Safe to
// call on an already-freed (base == nullptr) descriptor; resets all
// fields so a stale framebuffer can't accidentally render.
void Dath_FreeBitmap(struct DathFramebuffer *fb);

// Compose colors. RGBA8888 / BGRA8888 framebuffers store the alpha in
// the high byte; RGB565 packs into the low 16 bits and the high half
// is unused. Use Dath_RGB565 explicitly when the target framebuffer is
// DATH_FMT_RGB565 — the 32-bit composers do not auto-convert.
DathColor Dath_RGB(u8 r, u8 g, u8 b);
DathColor Dath_RGBA(u8 r, u8 g, u8 b, u8 a);
DathColor Dath_RGB565(u8 r, u8 g, u8 b);

void Dath_Pixel(const struct DathFramebuffer *fb, i32 x, i32 y, DathColor c);

// Filled rectangle, clipped to the framebuffer bounds. Negative
// origins or oversize widths/heights are handled by clipping.
void Dath_FillRect(const struct DathFramebuffer *fb, i32 x, i32 y, i32 w, i32 h, DathColor c);

// Convenience: fill the whole framebuffer with `c`.
void Dath_Clear(const struct DathFramebuffer *fb, DathColor c);

// Copy a w×h region from `src` at (sx, sy) to `dst` at (dx, dy).
// Both framebuffers must have the same format. Source and destination
// regions are clipped independently to their respective bounds.
void Dath_BlitRect(const struct DathFramebuffer *dst, i32 dx, i32 dy,
                   const struct DathFramebuffer *src, i32 sx, i32 sy, i32 w, i32 h);

// Bresenham line from (x0, y0) to (x1, y1) inclusive of both endpoints.
// Per-pixel writes go through Dath_Pixel so off-screen segments are
// transparently clipped.
void Dath_DrawLine(const struct DathFramebuffer *fb, i32 x0, i32 y0, i32 x1, i32 y1, DathColor c);

// Outlined rectangle — the four edges of the rect at (x, y) with size
// (w, h). Width 1 only; thicker borders compose multiple DrawRect
// calls. Edges are clipped to the framebuffer.
void Dath_DrawRect(const struct DathFramebuffer *fb, i32 x, i32 y, i32 w, i32 h, DathColor c);

// ---- FDT discovery ---------------------------------------------------------
//
// The pure parsed shape of a `simple-framebuffer` node — phys base
// + size, dimensions, stride, format. Kernel callers convert phys
// to a kernel VA via Mm_PhysToVirt and feed the result into
// Dath_Framebuffer_Init; host tests can verify the parser without
// caring about VA mapping.

struct DathFbDescriptor {
    u64 phys_base;
    u64 phys_size;
    u32 width;
    u32 height;
    u32 stride;
    DathFormat format;
};

// Walk the FDT for a node with compatible = "simple-framebuffer" and
// fill `out` from its reg / width / height / stride / format
// properties. Returns CARA_ENOTFOUND when no such node exists,
// CARA_EINVAL on a malformed node (e.g. unsupported format string),
// CARA_EOK on success.
[[nodiscard]] int Dath_Framebuffer_FromFdt(struct DathFbDescriptor *out, const struct Fdt *fdt);

// ---- Text rendering --------------------------------------------------------
//
// Bitmap fonts are stored as a flat array of one byte per glyph row,
// MSB = leftmost pixel. Width must be ≤ 8 in this Tier 1 cut so each
// row fits in a single byte. Codepoints below first_glyph or above
// last_glyph render as a solid background block.
//
// A complete public-domain font is future work — Phase 3 wants Latin /
// Greek / Cyrillic and a way to switch faces. For now dath_font_8x8
// covers space, exclamation, and uppercase A-Z, which is enough to
// prove text rendering plumbs end-to-end.

struct DathFont {
    u32 width;        // pixels per glyph (≤ 8)
    u32 height;       // pixels per glyph
    u32 first_glyph;  // codepoint of bitmap[0]
    u32 last_glyph;   // codepoint of last covered glyph (inclusive)
    const u8 *bitmap; // (last - first + 1) glyphs, height bytes each
};

extern const struct DathFont dath_font_8x8;

// Render a single character at (x, y) using `font`. fg is the
// foreground (set-bit) color; bg is the background (clear-bit) color.
void Dath_DrawChar(const struct DathFramebuffer *fb, const struct DathFont *font, i32 x, i32 y,
                   char c, DathColor fg, DathColor bg);

// Render `s` left-to-right at (x, y). Stops at the NUL terminator.
void Dath_DrawString(const struct DathFramebuffer *fb, const struct DathFont *font, i32 x, i32 y,
                     const char *s, DathColor fg, DathColor bg);

// ---- Console: text-rendering log sink ------------------------------------
//
// A DathConsole tracks (col, row) cursor state on top of a framebuffer +
// font and emits printable characters into the cell at the current
// position. Newline / carriage return are honoured; auto-scroll-up
// fires when the cursor would fall off the bottom edge.

struct DathConsole {
    struct DathFramebuffer *fb;
    const struct DathFont *font;
    u32 cur_col;
    u32 cur_row;
    u32 n_cols; // fb->width / font->width
    u32 n_rows; // fb->height / font->height
    DathColor fg;
    DathColor bg;
};

void Dath_Console_Init(struct DathConsole *con, struct DathFramebuffer *fb,
                       const struct DathFont *font, DathColor fg, DathColor bg);

// Append a single byte. Handled specials: '\n' (col=0, row++ + scroll),
// '\r' (col=0). Everything else is rendered as a glyph and the cursor
// advances one cell, wrapping at end of line.
void Dath_Console_PutChar(struct DathConsole *con, char c);

// Append a NUL-terminated string via PutChar.
void Dath_Console_PutString(struct DathConsole *con, const char *s);

// LogSink emit fn: feeds Log_FormatHuman output (ansi=false) into the
// console one byte at a time. Pass `&DathConsole` as the ctx when
// registering the sink with Log_RegisterSink.
struct LogRecord;
void Log_Sink_DathConsole_Emit(const struct LogRecord *r, void *ctx);

#endif
