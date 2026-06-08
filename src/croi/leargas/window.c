// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LD — window substrate, dual-target portion. InitInPlace,
// Render (decorations via Dath text + rect primitives), FromPub,
// LinkToScreen / UnlinkFromScreen all build for both host and
// kernel. The heap-allocating OpenWindow / CloseWindow live in
// window_alloc.c (kernel-only).

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>
#include <devices/inputevent.h>

#include <stddef.h> // offsetof

// Bounded copy mirroring screen.c — duplicated here so cara_leargas
// stays self-contained. NUL-terminates always.
static u32 copy_bounded(char *dst, u32 dst_cap, const char *src)
{
    if (!dst || dst_cap == 0) {
        return 0;
    }
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    u32 i = 0;
    while (i + 1 < dst_cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

[[nodiscard]] int Leargas_Window_InitInPlace(struct LeargasWindow *w, const struct NewWindow *nw)
{
    if (!w || !nw) {
        return CARA_EINVAL;
    }
    if (nw->Width <= 0 || nw->Height <= 0) {
        return CARA_EINVAL;
    }

    struct Screen *target = nw->Screen ? nw->Screen : Leargas_ActiveScreen();
    if (!target) {
        return CARA_EINVAL;
    }

    *w = (struct LeargasWindow){ 0 };
    w->idcmp_sigbit = -1; // no UserPort yet — OpenWindow may add one (LF)
    copy_bounded(w->title_buf, LEARGAS_WINDOW_TITLE_MAX, (const char *)nw->Title);

    // ---- V36+ public field set --------------------------------------------
    w->pub.NextWindow = nullptr;
    w->pub.LeftEdge = nw->LeftEdge;
    w->pub.TopEdge = nw->TopEdge;
    w->pub.Width = nw->Width;
    w->pub.Height = nw->Height;
    w->pub.MouseX = 0;
    w->pub.MouseY = 0;
    w->pub.MinWidth = nw->MinWidth;
    w->pub.MinHeight = nw->MinHeight;
    w->pub.MaxWidth = nw->MaxWidth;
    w->pub.MaxHeight = nw->MaxHeight;

    w->pub.Flags = nw->Flags;
    w->pub.MenuStrip = nullptr;
    w->pub.Title = (UBYTE *)w->title_buf;

    w->pub.FirstRequest = nullptr;
    w->pub.DMRequest = nullptr;
    w->pub.ReqCount = 0;

    w->pub.WScreen = target;

    // V36+ pragmatic deviation — these stay null in Phase 1.
    w->pub.RPort = nullptr;
    w->pub.BorderRPort = nullptr;
    w->pub.WLayer = nullptr;
    w->pub.IFont = nullptr;
    w->pub.Pointer = nullptr;
    w->pub.PtrHeight = 0;
    w->pub.PtrWidth = 0;
    w->pub.XOffset = 0;
    w->pub.YOffset = 0;
    w->pub.CheckMark = nullptr;

    // Border insets — title bar at top if WFLG_DRAGBAR is set;
    // borderless if WFLG_BORDERLESS is set.
    if (w->pub.Flags & WFLG_BORDERLESS) {
        w->pub.BorderTop = 0;
        w->pub.BorderBottom = 0;
        w->pub.BorderLeft = 0;
        w->pub.BorderRight = 0;
    } else {
        w->pub.BorderTop = (w->pub.Flags & WFLG_DRAGBAR) ? LEARGAS_WINDOW_DEFAULT_BORDER_TOP : 1;
        w->pub.BorderBottom = LEARGAS_WINDOW_DEFAULT_BORDER_BOTTOM;
        w->pub.BorderLeft = LEARGAS_WINDOW_DEFAULT_BORDER_LEFT;
        w->pub.BorderRight = LEARGAS_WINDOW_DEFAULT_BORDER_RIGHT;
    }

    w->pub.FirstGadget = nw->FirstGadget;
    w->pub.Parent = nullptr;
    w->pub.Descendant = nullptr;

    w->pub.IDCMPFlags = nw->IDCMPFlags;
    w->pub.UserPort = nullptr; // LF wires the IDCMP MsgPort
    w->pub.WindowPort = nullptr;
    w->pub.MessageKey = nullptr;

    w->pub.DetailPen = nw->DetailPen;
    w->pub.BlockPen = nw->BlockPen;
    w->pub.ScreenTitle = nullptr;

    w->pub.GZZMouseX = 0;
    w->pub.GZZMouseY = 0;
    w->pub.GZZWidth = 0;
    w->pub.GZZHeight = 0;

    w->pub.ExtData = nullptr;
    w->pub.UserData = nullptr;
    w->pub.MoreFlags = 0;

    w->initialised = true;
    return CARA_EOK;
}

[[nodiscard]] struct LeargasWindow *Leargas_Window_FromPub(struct Window *pub)
{
    if (!pub) {
        return nullptr;
    }
    return (struct LeargasWindow *)((char *)pub - offsetof(struct LeargasWindow, pub));
}

void Leargas_Window_LinkToScreen(struct LeargasWindow *w)
{
    if (!w || !w->pub.WScreen) {
        return;
    }
    w->pub.NextWindow = w->pub.WScreen->FirstWindow;
    w->pub.WScreen->FirstWindow = &w->pub;
}

void Leargas_Window_UnlinkFromScreen(struct LeargasWindow *w)
{
    if (!w || !w->pub.WScreen) {
        return;
    }
    struct Window **slot = &w->pub.WScreen->FirstWindow;
    while (*slot) {
        if (*slot == &w->pub) {
            *slot = w->pub.NextWindow;
            w->pub.NextWindow = nullptr;
            return;
        }
        slot = &(*slot)->NextWindow;
    }
}

// ---- Decoration rendering -------------------------------------------------
//
// Phase 1 hard-codes a small palette: a darker blue for the
// inactive window title bar / borders, a brighter blue for the
// active (focused) window's chrome, a lighter blue for body fill,
// white for the title text. LE flips active vs. inactive by reading
// WFLG_WINDOWACTIVE (set by Leargas_SetActiveWindow on focus change);
// a window opened without focus renders inactive, matching the LD
// behaviour the window unit test pins.

static DathColor pick_chrome_bg(struct DathFramebuffer *fb, bool active)
{
    // Active windows get a noticeably brighter title bar / frame so
    // the focused window reads at a glance — the Phase 1 stand-in for
    // V36+'s active-vs-inactive title-bar fill distinction.
    if (active) {
        if (fb->format == DATH_FMT_RGB565) {
            return Dath_RGB565(0x38, 0x78, 0xD0);
        }
        return Dath_RGB(0x38, 0x78, 0xD0);
    }
    if (fb->format == DATH_FMT_RGB565) {
        return Dath_RGB565(0x20, 0x40, 0x80);
    }
    return Dath_RGB(0x20, 0x40, 0x80);
}

static DathColor pick_chrome_fg(struct DathFramebuffer *fb)
{
    if (fb->format == DATH_FMT_RGB565) {
        return Dath_RGB565(0xFF, 0xFF, 0xFF);
    }
    return Dath_RGB(0xFF, 0xFF, 0xFF);
}

static DathColor pick_body(struct DathFramebuffer *fb)
{
    // Window body is a slightly lighter blue than the screen pen0
    // so the window's interior reads as distinct chrome from the
    // background. Phase 3 themes via the screen's DetailPen/BlockPen.
    if (fb->format == DATH_FMT_RGB565) {
        return Dath_RGB565(0x18, 0x30, 0x60);
    }
    return Dath_RGB(0x18, 0x30, 0x60);
}

void Leargas_Window_Render(struct LeargasWindow *w)
{
    if (!w || !w->initialised) {
        return;
    }
    struct LeargasScreen *ls = Leargas_Screen_FromPub(w->pub.WScreen);
    if (!ls || !ls->fb) {
        return;
    }
    struct DathFramebuffer *fb = ls->fb;

    bool active = (w->pub.Flags & WFLG_WINDOWACTIVE) != 0;
    DathColor chrome_bg = pick_chrome_bg(fb, active);
    DathColor chrome_fg = pick_chrome_fg(fb);
    DathColor body = pick_body(fb);

    i32 x0 = w->pub.LeftEdge;
    i32 y0 = w->pub.TopEdge;
    i32 ww = w->pub.Width;
    i32 hh = w->pub.Height;

    // Window body — fill the interior so any pre-existing screen
    // pixels under the window are clobbered cleanly.
    Dath_FillRect(fb, x0, y0, ww, hh, body);

    // Outer 1-pixel frame.
    Dath_DrawRect(fb, x0, y0, ww, hh, chrome_bg);

    // Title bar — only when DRAGBAR is requested.
    if (w->pub.Flags & WFLG_DRAGBAR) {
        i32 bar_h = w->pub.BorderTop;
        Dath_FillRect(fb, x0 + 1, y0 + 1, ww - 2, bar_h - 1, chrome_bg);

        // Title text — clipped horizontally if the title's wider
        // than the bar minus the close-gadget reserve.
        i32 reserve = (w->pub.Flags & WFLG_CLOSEGADGET) ? bar_h : 0;
        if (w->pub.Title) {
            i32 text_y = y0 + (bar_h - (i32)dath_font_8x8.height) / 2;
            if (text_y < y0 + 1) {
                text_y = y0 + 1;
            }
            // Clip text to bar interior by drawing into a rect
            // restricted in the body color check on each glyph;
            // Dath_DrawString clips per-pixel against fb extent
            // but not against an arbitrary subregion, so we limit
            // by string length here.
            i32 max_chars = (ww - 4 - reserve) / (i32)dath_font_8x8.width;
            if (max_chars > 0) {
                char tmp[LEARGAS_WINDOW_TITLE_MAX];
                u32 n = 0;
                while (n + 1 < sizeof(tmp) && (i32)n < max_chars && w->title_buf[n] != '\0') {
                    tmp[n] = w->title_buf[n];
                    n++;
                }
                tmp[n] = '\0';
                Dath_DrawString(fb, &dath_font_8x8, x0 + 4, text_y, tmp, chrome_fg, chrome_bg);
            }
        }

        // Close gadget — small rectangle on the right.
        if (w->pub.Flags & WFLG_CLOSEGADGET) {
            i32 cg_w = bar_h;
            i32 cg_x = x0 + ww - cg_w;
            // Close gadget background.
            Dath_FillRect(fb, cg_x, y0 + 1, cg_w - 1, bar_h - 1, body);
            Dath_DrawRect(fb, cg_x, y0, cg_w, bar_h, chrome_bg);
            // Diagonal "X" inside the gadget.
            i32 ix0 = cg_x + 2;
            i32 iy0 = y0 + 2;
            i32 ix1 = cg_x + cg_w - 3;
            i32 iy1 = y0 + bar_h - 3;
            Dath_DrawLine(fb, ix0, iy0, ix1, iy1, chrome_fg);
            Dath_DrawLine(fb, ix0, iy1, ix1, iy0, chrome_fg);
        }
    }

    // LG — app gadgets sit on top of the window chrome.
    Leargas_Window_RenderGadgets(&w->pub);
}

// ---- LI — close-gadget hit-test (dual-target, pure) -----------------------

bool Leargas_Window_CloseHitTest(struct Window *w, i32 wx, i32 wy)
{
    if (!w) {
        return false;
    }
    // The close gadget is only drawn when both flags are set (see
    // Leargas_Window_Render): a BorderTop-wide square at the top-right
    // of the title bar.
    if ((w->Flags & (WFLG_DRAGBAR | WFLG_CLOSEGADGET)) != (WFLG_DRAGBAR | WFLG_CLOSEGADGET)) {
        return false;
    }
    i32 bar_h = w->BorderTop;
    i32 ww = w->Width;
    if (bar_h <= 0 || ww <= 0) {
        return false;
    }
    i32 cg_x0 = ww - bar_h; // window-relative left edge of the close box
    return wx >= cg_x0 && wx < ww && wy >= 0 && wy < bar_h;
}

// ---- LF — IntuiMessage translation (dual-target, pure) --------------------

void Leargas_BuildIntuiMessage(struct IntuiMessage *im, struct Window *w,
                               const struct LeargasInputEvent *ev)
{
    if (!im || !w || !ev) {
        return;
    }

    // Map the flat input class to its IDCMP class. RAWKEY → IDCMP_RAWKEY
    // (LF); a RAWMOUSE button event → IDCMP_MOUSEBUTTONS while pure
    // motion → IDCMP_MOUSEMOVE (LI). Other classes resolve to 0 until
    // their epics land.
    ULONG cls = 0;
    if (ev->ie_class == IECLASS_RAWKEY) {
        cls = IDCMP_RAWKEY;
    } else if (ev->ie_class == IECLASS_RAWMOUSE) {
        cls = (ev->ie_code == IECODE_NOBUTTON) ? IDCMP_MOUSEMOVE : IDCMP_MOUSEBUTTONS;
    }

    im->ExecMessage.mn_Length = (UWORD)sizeof(struct IntuiMessage);
    im->Class = cls;
    im->Code = ev->ie_code;
    im->Qualifier = ev->ie_qualifier;
    im->IAddress = nullptr;

    // Window-relative pointer position at the time of the event. The
    // router mirrors the (clamped) pointer into WScreen->MouseX/Y;
    // subtract the window origin. No screen → leave at origin.
    if (w->WScreen) {
        im->MouseX = (WORD)(w->WScreen->MouseX - w->LeftEdge);
        im->MouseY = (WORD)(w->WScreen->MouseY - w->TopEdge);
    } else {
        im->MouseX = 0;
        im->MouseY = 0;
    }

    // Split the boot-time nanosecond stamp into Exec's seconds/micros.
    im->Seconds = (ULONG)(ev->ie_ts_ns / 1000000000ull);
    im->Micros = (ULONG)((ev->ie_ts_ns % 1000000000ull) / 1000ull);

    im->IDCMPWindow = w;
    im->SpecialLink = nullptr;
}
