// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LG — gadget framework (dual-target). Chain management
// (AddGadget / RemoveGadget), hit-testing, and rendering of a
// button-style gadget face + GadgetText label, plus the single
// process-wide active-gadget pointer (input focus). All pure list ops
// + Dath rendering, so the same code builds for host unit tests and the
// kernel. The router (router.c) drives press/select on a left-button
// down; IDCMP_GADGETUP/GADGETDOWN and string editing arrive in LH.

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>

// ---- Active gadget (input focus) ------------------------------------------

static struct Gadget *g_active_gadget;

[[nodiscard]] struct Gadget *Leargas_ActiveGadget(void)
{
    return g_active_gadget;
}

void Leargas_SetActiveGadget(struct Gadget *g)
{
    g_active_gadget = g;
}

void Leargas_Gadget_Reset(void)
{
    g_active_gadget = nullptr;
}

// ---- GADGETUP delivery hook (general; LH string Inntin + LJ boolean) -------
//
// Kept here (a plain function pointer) so this dual-target file carries
// no kernel dependency; the kernel installs Leargas_IDCMP_PostGadgetUp.
// Both the string Inntin (Return commit, string_gadget.c) and a boolean
// gadget release (router.c) post through Leargas_Gadget_RouteUp.
static Leargas_GadgetUpFn g_gadgetup_router = nullptr;

void Leargas_SetGadgetRouter(Leargas_GadgetUpFn fn)
{
    g_gadgetup_router = fn;
}

bool Leargas_Gadget_RouteUp(struct Window *w, struct Gadget *g)
{
    if (g_gadgetup_router && w && g) {
        return g_gadgetup_router(w, g);
    }
    return false;
}

// ---- Chain management ------------------------------------------------------

void Leargas_AddGadget(struct Window *w, struct Gadget *g)
{
    if (!w || !g) {
        return;
    }
    struct Gadget **slot = &w->FirstGadget;
    while (*slot) {
        if (*slot == g) {
            return; // already linked — don't corrupt the chain
        }
        slot = &(*slot)->NextGadget;
    }
    g->NextGadget = nullptr;
    *slot = g;
}

void Leargas_RemoveGadget(struct Window *w, struct Gadget *g)
{
    if (!w || !g) {
        return;
    }
    struct Gadget **slot = &w->FirstGadget;
    while (*slot) {
        if (*slot == g) {
            *slot = g->NextGadget;
            g->NextGadget = nullptr;
            return;
        }
        slot = &(*slot)->NextGadget;
    }
}

// ---- Hit-testing -----------------------------------------------------------

[[nodiscard]] struct Gadget *Leargas_Gadget_HitTest(struct Window *w, i32 wx, i32 wy)
{
    if (!w) {
        return nullptr;
    }
    for (struct Gadget *g = w->FirstGadget; g; g = g->NextGadget) {
        if (g->Flags & GFLG_DISABLED) {
            continue; // ghosted gadgets ignore input
        }
        i32 gx0 = g->LeftEdge;
        i32 gy0 = g->TopEdge;
        i32 gx1 = gx0 + g->Width;  // right edge, exclusive
        i32 gy1 = gy0 + g->Height; // bottom edge, exclusive
        if (wx >= gx0 && wx < gx1 && wy >= gy0 && wy < gy1) {
            return g;
        }
    }
    return nullptr;
}

// ---- Rendering -------------------------------------------------------------
//
// Phase 1 paints gadgets as grey buttons so they read distinct from the
// blue window chrome: a lighter face at rest, a darker "pressed" face
// when GFLG_SELECTED, a mid grey when GFLG_DISABLED. Phase 3 themes via
// GadgetRender Images / Borders + the screen's pens.

static DathColor grey(struct DathFramebuffer *fb, u8 v)
{
    if (fb->format == DATH_FMT_RGB565) {
        return Dath_RGB565(v, v, v);
    }
    return Dath_RGB(v, v, v);
}

void Leargas_Gadget_Render(struct Window *w, struct Gadget *g)
{
    if (!w || !g) {
        return;
    }

    // LH — string gadgets render as text fields, not buttons.
    if ((g->GadgetType & GTYP_GTYPEMASK) == GTYP_STRGADGET) {
        Leargas_StringGadget_Render(w, g);
        return;
    }
    // L8.5 — proportional gadgets render as a container + a draggable knob.
    if ((g->GadgetType & GTYP_GTYPEMASK) == GTYP_PROPGADGET) {
        Leargas_Prop_Render(w, g);
        return;
    }

    struct LeargasScreen *ls = Leargas_Screen_FromPub(w->WScreen);
    if (!ls || !ls->fb) {
        return;
    }
    struct DathFramebuffer *fb = ls->fb;

    bool selected = (g->Flags & GFLG_SELECTED) != 0;
    bool disabled = (g->Flags & GFLG_DISABLED) != 0;

    i32 x0 = (i32)w->LeftEdge + (i32)g->LeftEdge;
    i32 y0 = (i32)w->TopEdge + (i32)g->TopEdge;
    i32 gw = g->Width;
    i32 gh = g->Height;
    if (gw <= 0 || gh <= 0) {
        return;
    }

    DathColor face = disabled ? grey(fb, 0x88) : grey(fb, selected ? 0x70 : 0xB0);
    DathColor border = grey(fb, 0x30);
    DathColor text = disabled ? grey(fb, 0x60) : grey(fb, 0x00);

    Dath_FillRect(fb, x0, y0, gw, gh, face);
    Dath_DrawRect(fb, x0, y0, gw, gh, border);

    struct IntuiText *it = g->GadgetText;
    if (it && it->IText) {
        i32 tx = x0 + (i32)it->LeftEdge;
        i32 ty = y0 + (i32)it->TopEdge;
        Dath_DrawString(fb, &dath_font_8x8, tx, ty, (const char *)it->IText, text, face);
    }
}

void Leargas_Window_RenderGadgets(struct Window *w)
{
    if (!w) {
        return;
    }
    for (struct Gadget *g = w->FirstGadget; g; g = g->NextGadget) {
        Leargas_Gadget_Render(w, g);
    }
}
