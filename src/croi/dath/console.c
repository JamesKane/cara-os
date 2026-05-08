// SPDX-License-Identifier: BSD-2-Clause
//
// DathConsole — a "VT-flavoured" text overlay on top of a DathFramebuffer.
// Tier 1 only: printable ASCII via the bound DathFont, '\n' for line
// break with auto-scroll, '\r' for column reset, end-of-line wrap.
// No tabs, no escape sequences, no attributes — Phase 3 wants ANSI
// passthrough but it lands once Leargas needs styled text.

#include <cara/dath.h>
#include <cara/log.h>
#include <cara/types.h>

void Dath_Console_Init(struct DathConsole *con, struct DathFramebuffer *fb,
                       const struct DathFont *font,
                       DathColor fg, DathColor bg)
{
    if (!con || !fb || !font || font->width == 0 || font->height == 0) {
        return;
    }
    con->fb = fb;
    con->font = font;
    con->cur_col = 0;
    con->cur_row = 0;
    con->n_cols = fb->width / font->width;
    con->n_rows = fb->height / font->height;
    con->fg = fg;
    con->bg = bg;
}

// Scroll the framebuffer content up by one font row, then clear the
// freshly-vacated bottom row. Dath_BlitRect copies row-by-row top to
// bottom; for a scroll-up (dst above src) that direction is overlap-
// safe — every read happens before the destination row is overwritten.
static void scroll_up_one_row(struct DathConsole *con)
{
    u32 fh = con->font->height;
    u32 px_h = con->n_rows * fh;
    u32 fb_w = con->fb->width;

    Dath_BlitRect(con->fb, 0, 0, con->fb, 0, (i32)fh, (i32)fb_w,
                  (i32)(px_h - fh));
    Dath_FillRect(con->fb, 0, (i32)(px_h - fh), (i32)fb_w, (i32)fh, con->bg);
}

static void newline(struct DathConsole *con)
{
    con->cur_col = 0;
    con->cur_row++;
    if (con->cur_row >= con->n_rows) {
        scroll_up_one_row(con);
        con->cur_row = con->n_rows - 1;
    }
}

void Dath_Console_PutChar(struct DathConsole *con, char c)
{
    if (!con || !con->fb || !con->font) {
        return;
    }
    if (c == '\n') {
        newline(con);
        return;
    }
    if (c == '\r') {
        con->cur_col = 0;
        return;
    }
    if (c == '\t') {
        // Soft tab to next 8-column stop.
        u32 next = (con->cur_col + 8u) & ~7u;
        while (con->cur_col < next && con->cur_col < con->n_cols) {
            Dath_DrawChar(con->fb, con->font,
                          (i32)(con->cur_col * con->font->width),
                          (i32)(con->cur_row * con->font->height),
                          ' ', con->fg, con->bg);
            con->cur_col++;
        }
        if (con->cur_col >= con->n_cols) {
            newline(con);
        }
        return;
    }
    if (c < 0x20) {
        // Drop other control chars silently.
        return;
    }

    Dath_DrawChar(con->fb, con->font,
                  (i32)(con->cur_col * con->font->width),
                  (i32)(con->cur_row * con->font->height),
                  c, con->fg, con->bg);
    con->cur_col++;
    if (con->cur_col >= con->n_cols) {
        newline(con);
    }
}

void Dath_Console_PutString(struct DathConsole *con, const char *s)
{
    if (!con || !s) {
        return;
    }
    while (*s) {
        Dath_Console_PutChar(con, *s++);
    }
}

void Log_Sink_DathConsole_Emit(const struct LogRecord *r, void *ctx)
{
    struct DathConsole *con = (struct DathConsole *)ctx;
    if (!con || !r) {
        return;
    }
    char buf[CARA_LOG_RECORD_BYTES + 64];
    usize n = Log_FormatHuman(buf, sizeof(buf), r, /*ansi=*/false);
    for (usize i = 0; i < n; i++) {
        Dath_Console_PutChar(con, buf[i]);
    }
}
