// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas proportional-gadget support (L8.5, docs/LEARGAS_GADTOOLS.md
// §4): render a GTYP_PROPGADGET as a recessed container + a raised knob
// positioned/sized by its PropInfo, and recompute the knob's pot from a
// window-relative pointer position as it is dragged. The Leargas input
// router calls Leargas_Prop_HandleDrag on a button-held mouse move over
// a selected prop gadget; gadtools' SLIDER/SCROLLER kinds map their
// level/scroll attributes onto Horiz/VertPot.
//
// Dual-target (host unit tests draw to a memory framebuffer), so no
// kernel deps — plain Dath primitives over the screen surface.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>
#include <intuition/intuition.h>

static DathColor pg_grey(struct DathFramebuffer *fb, u8 v)
{
    if (fb->format == DATH_FMT_RGB565) {
        return Dath_RGB565(v, v, v);
    }
    return Dath_RGB(v, v, v);
}

static i32 pg_clamp(i32 v, i32 lo, i32 hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

// Knob extent (offset + size) along one axis, from a pot/body pair and
// the container's inner pixel span. *off and *size are in pixels.
static void pg_knob(UWORD pot, UWORD body, i32 span, i32 kmin, i32 *off, i32 *size)
{
    i32 ksize = (i32)(((u32)body * (u32)span) / MAXBODY);
    if (ksize < kmin) {
        ksize = kmin;
    }
    if (ksize > span) {
        ksize = span;
    }
    i32 travel = span - ksize;
    i32 koff = (i32)(((u32)pot * (u32)travel) / MAXPOT);
    *off = pg_clamp(koff, 0, travel);
    *size = ksize;
}

void Leargas_Prop_Render(struct Window *w, struct Gadget *g)
{
    if (!w || !g || !g->SpecialInfo) {
        return;
    }
    struct PropInfo *pi = (struct PropInfo *)g->SpecialInfo;
    struct LeargasScreen *ls = Leargas_Screen_FromPub(w->WScreen);
    if (!ls || !ls->fb) {
        return;
    }
    struct DathFramebuffer *fb = ls->fb;

    i32 x0 = (i32)w->LeftEdge + (i32)g->LeftEdge;
    i32 y0 = (i32)w->TopEdge + (i32)g->TopEdge;
    i32 gw = g->Width;
    i32 gh = g->Height;
    if (gw <= 0 || gh <= 0) {
        return;
    }
    // Cache the container pixel dims for the drag math.
    pi->CWidth = (UWORD)gw;
    pi->CHeight = (UWORD)gh;

    DathColor cont = pg_grey(fb, 0x60); // recessed container
    DathColor border = pg_grey(fb, 0x30);
    DathColor knob = pg_grey(fb, 0xC0); // raised knob

    Dath_FillRect(fb, x0, y0, gw, gh, cont);
    Dath_DrawRect(fb, x0, y0, gw, gh, border);

    i32 koff, ksize;
    if (pi->Flags & FREEVERT) {
        pg_knob(pi->VertPot, pi->VertBody ? pi->VertBody : MAXBODY, gh, KNOBVMIN, &koff, &ksize);
        Dath_FillRect(fb, x0 + 1, y0 + koff, gw - 2, ksize, knob);
        Dath_DrawRect(fb, x0 + 1, y0 + koff, gw - 2, ksize, border);
    } else { // FREEHORIZ (default)
        pg_knob(pi->HorizPot, pi->HorizBody ? pi->HorizBody : MAXBODY, gw, KNOBHMIN, &koff, &ksize);
        Dath_FillRect(fb, x0 + koff, y0 + 1, ksize, gh - 2, knob);
        Dath_DrawRect(fb, x0 + koff, y0 + 1, ksize, gh - 2, border);
    }
}

// Recompute the pot from a window-relative pointer (wx,wy): the pointer's
// position within the gadget maps linearly to 0..MAXPOT. Returns true if
// the pot changed.
bool Leargas_Prop_HandleDrag(struct Gadget *g, i32 wx, i32 wy)
{
    if (!g || !g->SpecialInfo) {
        return false;
    }
    struct PropInfo *pi = (struct PropInfo *)g->SpecialInfo;
    bool vert = (pi->Flags & FREEVERT) != 0;
    i32 span = vert ? g->Height : g->Width;
    i32 pos = vert ? (wy - (i32)g->TopEdge) : (wx - (i32)g->LeftEdge);
    if (span <= 1) {
        return false;
    }
    i32 pot = (i32)(((u32)pg_clamp(pos, 0, span - 1) * MAXPOT) / (u32)(span - 1));
    pot = pg_clamp(pot, 0, MAXPOT);
    UWORD newpot = (UWORD)pot;
    UWORD *slot = vert ? &pi->VertPot : &pi->HorizPot;
    if (*slot == newpot) {
        return false;
    }
    *slot = newpot;
    return true;
}
