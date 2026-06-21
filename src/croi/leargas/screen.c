// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LB — screen substrate, dual-target portion. InitInPlace,
// SetActive, ActiveScreen, FromPub all run unchanged in host unit
// tests and the kernel image. The heap-allocating OpenScreen /
// CloseScreen live in screen_alloc.c (kernel-only, depends on
// Croi_Alloc).

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>

#include <stddef.h> // offsetof

// Active screen — Phase 1 carries exactly one. Phase 3 widens this
// to a list with depth-arrange semantics; Leargas_ActiveScreen() is
// the only public way to read it so the seam is preserved.
static struct LeargasScreen *g_active_screen;

// Strict-bounded copy: copies up to dst_cap-1 bytes from src and
// always NUL-terminates. Returns the number of bytes written
// (excluding the NUL). Local to keep cara_leargas free of a
// dependency on cara_util's string helpers.
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

[[nodiscard]] int Leargas_Screen_InitInPlace(struct LeargasScreen *s, struct DathFramebuffer *fb,
                                             const char *title, DathColor pen0)
{
    if (!s || !fb) {
        return CARA_EINVAL;
    }
    if (fb->width == 0 || fb->height == 0) {
        return CARA_EINVAL;
    }

    *s = (struct LeargasScreen){ 0 };
    s->fb = fb;
    s->pen0 = pen0;

    copy_bounded(s->title_buf, LEARGAS_SCREEN_TITLE_MAX, title);
    copy_bounded(s->default_title_buf, LEARGAS_SCREEN_TITLE_MAX, title);

    // V36+ public Screen field set. Phase 1 carries pointers where
    // V36+ embeds (per file-banner deviation in intuition/screens.h);
    // those pointer fields stay nullptr because Leargas drives the
    // surface directly via the brand-private DathFramebuffer.
    s->pub.NextScreen = nullptr;
    s->pub.FirstWindow = nullptr;
    s->pub.LeftEdge = 0;
    s->pub.TopEdge = 0;
    s->pub.Width = (WORD)fb->width;
    s->pub.Height = (WORD)fb->height;
    s->pub.MouseX = 0;
    s->pub.MouseY = 0;
    s->pub.Flags = WBENCHSCREEN | SHOWTITLE;

    s->pub.Title = (UBYTE *)s->title_buf;
    s->pub.DefaultTitle = (UBYTE *)s->default_title_buf;

    s->pub.BarHeight = LEARGAS_DEFAULT_BAR_HEIGHT;
    s->pub.BarVBorder = LEARGAS_DEFAULT_BAR_VBORDER;
    s->pub.BarHBorder = LEARGAS_DEFAULT_BAR_HBORDER;
    s->pub.MenuVBorder = LEARGAS_DEFAULT_MENU_VBORDER;
    s->pub.MenuHBorder = LEARGAS_DEFAULT_MENU_HBORDER;
    s->pub.WBorTop = LEARGAS_DEFAULT_WBOR_TOP;
    s->pub.WBorLeft = LEARGAS_DEFAULT_WBOR_LEFT;
    s->pub.WBorRight = LEARGAS_DEFAULT_WBOR_RIGHT;
    s->pub.WBorBottom = LEARGAS_DEFAULT_WBOR_BOTTOM;

    s->pub.Font = nullptr;
    s->pub.ViewPort = nullptr;
    s->pub.RastPort = nullptr;
    s->pub.BitMap = nullptr;
    s->pub.LayerInfo = nullptr;
    s->pub.FirstGadget = nullptr;
    s->pub.DetailPen = 0;
    s->pub.BlockPen = 1;
    s->pub.SaveColor0 = 0;
    s->pub.BarLayer = nullptr;
    s->pub.ExtData = nullptr;
    s->pub.UserData = nullptr;

    // L4.7: build a live RastPort + BitMap over the screen framebuffer.
    // The BitMap is the head of a DathBitMapExt whose surf mirrors *fb,
    // so graphics.library calls (surf_of) draw to the same pixels the
    // Leargas chrome renders to. Pure data setup — no Croi_Gfx call, so
    // this stays dual-target (the RastPort defaults mirror InitRastPort).
    s->bmext.surf = *fb;
    s->bmext.bm = (struct BitMap){ 0 };
    s->bmext.bm.BytesPerRow = (UWORD)fb->stride;
    s->bmext.bm.Rows = (UWORD)fb->height;
    s->bmext.bm.Depth = (UBYTE)(fb->bpp == 2 ? 16 : 32);
    s->bmext.bm.Planes[0] = (PLANEPTR)fb->base;

    s->rp = (struct RastPort){ 0 };
    s->rp.BitMap = &s->bmext.bm;
    s->rp.Mask = 0xFF;
    s->rp.FgPen = 1;
    s->rp.BgPen = 0;
    s->rp.DrawMode = JAM2;
    s->rp.LinePtrn = (UWORD)0xFFFF;
    s->rp.PenWidth = 1;
    s->rp.PenHeight = 1;

    s->pub.RastPort = &s->rp;
    s->pub.BitMap = &s->bmext.bm;

    s->initialised = true;
    return CARA_EOK;
}

void Leargas_Screen_SetActive(struct LeargasScreen *s)
{
    g_active_screen = s;
}

// L5.6: the boot display framebuffer OpenScreen opens custom screens over.
static struct DathFramebuffer *g_display_fb;

void Leargas_SetDisplayFramebuffer(struct DathFramebuffer *fb)
{
    g_display_fb = fb;
}

[[nodiscard]] struct DathFramebuffer *Leargas_DisplayFramebuffer(void)
{
    return g_display_fb;
}

[[nodiscard]] struct Screen *Leargas_ActiveScreen(void)
{
    return g_active_screen ? &g_active_screen->pub : nullptr;
}

[[nodiscard]] struct LeargasScreen *Leargas_Screen_FromPub(struct Screen *pub)
{
    if (!pub) {
        return nullptr;
    }
    return (struct LeargasScreen *)((char *)pub - offsetof(struct LeargasScreen, pub));
}
