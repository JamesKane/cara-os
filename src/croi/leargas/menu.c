// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas — menu strips (L5.3, docs/LEARGAS_INTUITION.md §2.3). The data
// model (SetMenuStrip/ClearMenuStrip/ItemAddress), layout, render, and
// menu-button interaction. Dual-target: pure layout/hit-test + Dath
// rendering, no kernel deps (the IDCMP_MENUPICK delivery is the kernel's
// PostMenuPick, called from the router via an installed hook).
//
// v0 interaction (simplified): RMB-down over the active window's menu
// strip renders the bar + all dropdowns at once; RMB-up hit-tests the
// pointer and posts IDCMP_MENUPICK with the packed FULLMENUNUM (or
// MENUNULL). Switching/erasing a single dropdown on motion (the real
// model) + sub-menus are deferred (§6.2). Exiting menu mode full-refreshes
// the screen (no Layers/backing store).

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>
#include <intuition/intuition.h>

// The window currently in menu mode, or null. Process-wide (one pointer).
static struct Window *g_menu_win;

static u32 menu_strlen(const BYTE *s)
{
    u32 n = 0;
    while (s && s[n]) {
        n++;
    }
    return n;
}

// A menu item's display text (ITEMTEXT items carry an IntuiText in
// ItemFill). nullptr for image items (unsupported in v0).
static const char *item_text(const struct MenuItem *it)
{
    if (it && (it->Flags & ITEMTEXT) && it->ItemFill) {
        return (const char *)((const struct IntuiText *)it->ItemFill)->IText;
    }
    return nullptr;
}

static i32 menu_bar_height(void)
{
    return (i32)dath_font_8x8.height + 2;
}

// Lay out menu headers along the bar and each menu's dropdown item boxes.
// Geometry is stored back into the Menu/MenuItem structs (the V36
// contract — Intuition fills these at SetMenuStrip time).
void Leargas_Menu_Layout(struct Menu *menu, struct Screen *screen)
{
    (void)screen;
    i32 fw = (i32)dath_font_8x8.width;
    i32 fh = (i32)dath_font_8x8.height;
    i32 bar_h = menu_bar_height();
    i32 x = 4;
    for (struct Menu *m = menu; m; m = m->NextMenu) {
        i32 name_w = (i32)menu_strlen(m->MenuName) * fw;
        m->LeftEdge = (WORD)x;
        m->TopEdge = 0;
        m->Width = (WORD)(name_w + 8);
        m->Height = (WORD)bar_h;

        i32 max_w = 0;
        for (struct MenuItem *it = m->FirstItem; it; it = it->NextItem) {
            const char *t = item_text(it);
            i32 w = (t ? (i32)menu_strlen((const BYTE *)t) : 0) * fw + 8;
            if (w > max_w) {
                max_w = w;
            }
        }
        i32 iy = bar_h;
        for (struct MenuItem *it = m->FirstItem; it; it = it->NextItem) {
            it->LeftEdge = m->LeftEdge;
            it->TopEdge = (WORD)iy;
            it->Width = (WORD)max_w;
            it->Height = (WORD)(fh + 2);
            iy += fh + 2;
        }
        x += m->Width + 4;
    }
}

void Leargas_SetMenuStrip(struct Window *w, struct Menu *menu)
{
    if (!w) {
        return;
    }
    w->MenuStrip = menu;
    Leargas_Menu_Layout(menu, w->WScreen);
}

void Leargas_ClearMenuStrip(struct Window *w)
{
    if (w) {
        w->MenuStrip = nullptr;
    }
}

// Resolve a packed menu number to its MenuItem (ItemAddress).
struct MenuItem *Leargas_ItemAddress(struct Menu *menu, u16 code)
{
    if (code == MENUNULL) {
        return nullptr;
    }
    u16 mn = MENUNUM(code);
    u16 in = ITEMNUM(code);
    u16 sn = SUBNUM(code);
    struct Menu *m = menu;
    for (u16 i = 0; i < mn && m; i++) {
        m = m->NextMenu;
    }
    if (!m || in == NOITEM) {
        return nullptr;
    }
    struct MenuItem *it = m->FirstItem;
    for (u16 j = 0; j < in && it; j++) {
        it = it->NextItem;
    }
    if (!it) {
        return nullptr;
    }
    if (sn != NOSUB && it->SubItem) {
        struct MenuItem *s = it->SubItem;
        for (u16 k = 0; k < sn && s; k++) {
            s = s->NextItem;
        }
        return s;
    }
    return it;
}

// ---- Rendering (Dath) -----------------------------------------------------

static DathColor mc(struct DathFramebuffer *fb, u8 r, u8 g, u8 b)
{
    return fb->format == DATH_FMT_RGB565 ? Dath_RGB565(r, g, b) : Dath_RGB(r, g, b);
}

static void render_bar(struct LeargasScreen *ls)
{
    struct DathFramebuffer *fb = ls->fb;
    struct Window *w = g_menu_win;
    if (!w || !w->MenuStrip) {
        return;
    }
    i32 bar_h = menu_bar_height();
    DathColor bar_bg = mc(fb, 0xC0, 0xC0, 0xC0);
    DathColor txt = mc(fb, 0x00, 0x00, 0x00);
    DathColor box = mc(fb, 0xE0, 0xE0, 0xE0);
    DathColor edge = mc(fb, 0x40, 0x40, 0x40);

    Dath_FillRect(fb, 0, 0, (i32)fb->width, bar_h, bar_bg);
    for (struct Menu *m = w->MenuStrip; m; m = m->NextMenu) {
        Dath_DrawString(fb, &dath_font_8x8, m->LeftEdge + 4, 1, (const char *)m->MenuName, txt,
                        bar_bg);
        // Dropdown box + items (v0 shows all menus' dropdowns at once).
        for (struct MenuItem *it = m->FirstItem; it; it = it->NextItem) {
            const char *t = item_text(it);
            Dath_FillRect(fb, it->LeftEdge, it->TopEdge, it->Width, it->Height, box);
            Dath_DrawRect(fb, it->LeftEdge, it->TopEdge, it->Width, it->Height, edge);
            if (t) {
                Dath_DrawString(fb, &dath_font_8x8, it->LeftEdge + 4, it->TopEdge + 1, t, txt, box);
            }
        }
    }
}

// Repaint the screen background + every window (no Layers/backing store,
// so menu teardown redraws from scratch).
static void full_refresh(struct LeargasScreen *ls)
{
    Dath_Clear(ls->fb, ls->pen0);
    for (struct Window *p = ls->pub.FirstWindow; p; p = p->NextWindow) {
        struct LeargasWindow *lw = Leargas_Window_FromPub(p);
        if (lw) {
            Leargas_Window_Render(lw);
        }
    }
}

// ---- Interaction ----------------------------------------------------------

// RMB-down: enter menu mode and render the bar. Returns true if the
// window has a menu strip (menu mode entered).
bool Leargas_Menu_Begin(struct Window *w)
{
    if (!w || !w->MenuStrip || !w->WScreen) {
        return false;
    }
    g_menu_win = w;
    struct LeargasScreen *ls = Leargas_Screen_FromPub(w->WScreen);
    if (ls && ls->fb) {
        render_bar(ls);
    }
    return true;
}

// RMB-up: hit-test (x,y) against the laid-out items, fill *code with the
// packed pick (MENUNULL if none), restore the screen, leave menu mode.
// Returns true if menu mode was active (so the caller posts IDCMP_MENUPICK).
bool Leargas_Menu_End(i32 x, i32 y, struct Window **win_out, u16 *code_out)
{
    struct Window *w = g_menu_win;
    if (!w) {
        return false;
    }
    u16 code = MENUNULL;
    u16 mi = 0;
    for (struct Menu *m = w->MenuStrip; m; m = m->NextMenu, mi++) {
        u16 ii = 0;
        for (struct MenuItem *it = m->FirstItem; it; it = it->NextItem, ii++) {
            if (!(it->Flags & ITEMENABLED)) {
                continue;
            }
            if (x >= it->LeftEdge && x < it->LeftEdge + it->Width && y >= it->TopEdge &&
                y < it->TopEdge + it->Height) {
                code = (u16)FULLMENUNUM(mi, ii, NOSUB);
            }
        }
    }
    g_menu_win = nullptr;
    struct LeargasScreen *ls = Leargas_Screen_FromPub(w->WScreen);
    if (ls && ls->fb) {
        full_refresh(ls);
    }
    if (win_out) {
        *win_out = w;
    }
    if (code_out) {
        *code_out = code;
    }
    return true;
}

void Leargas_Menu_Reset(void)
{
    g_menu_win = nullptr;
}
