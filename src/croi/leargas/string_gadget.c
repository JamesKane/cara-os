// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LH — the string Inntin (text input gadget), dual-target. A
// built-in US keymap decodes V36+ rawkeys to ASCII (Phase 3's
// keymap.library replaces it); StringInfo editing inserts / deletes in
// the client's buffer; the gadget renders as a recessed field with a
// cursor; and String_RouteKey lets the active string gadget consume
// keystrokes, posting IDCMP_GADGETUP on Return through a kernel-installed
// hook (the only non-dual-target seam — same shape as the LF key router).

#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>
#include <devices/inputevent.h>

// ---- US keymap: V36+ rawkey -> ASCII --------------------------------------
//
// The Amiga rawkey codes are positional (see src/croi/hid/hid.c). These
// two tables are the unshifted faces and the shifted faces of the
// printable keys; letters carry 0 in the shift table and are uppercased
// arithmetically so CapsLock can compose with Shift.

static const char k_base[0x46] = {
    [0x00] = '`',  [0x01] = '1',  [0x02] = '2', [0x03] = '3', [0x04] = '4', [0x05] = '5',
    [0x06] = '6',  [0x07] = '7',  [0x08] = '8', [0x09] = '9', [0x0A] = '0', [0x0B] = '-',
    [0x0C] = '=',  [0x0D] = '\\', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't',  [0x15] = 'y',  [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[',  [0x1B] = ']',  [0x20] = 'a', [0x21] = 's', [0x22] = 'd', [0x23] = 'f',
    [0x24] = 'g',  [0x25] = 'h',  [0x26] = 'j', [0x27] = 'k', [0x28] = 'l', [0x29] = ';',
    [0x2A] = '\'', [0x2B] = '#',  [0x31] = 'z', [0x32] = 'x', [0x33] = 'c', [0x34] = 'v',
    [0x35] = 'b',  [0x36] = 'n',  [0x37] = 'm', [0x38] = ',', [0x39] = '.', [0x3A] = '/',
    [0x40] = ' ',
};

static const char k_shift[0x46] = {
    [0x00] = '~', [0x01] = '!', [0x02] = '@', [0x03] = '#', [0x04] = '$', [0x05] = '%',
    [0x06] = '^', [0x07] = '&', [0x08] = '*', [0x09] = '(', [0x0A] = ')', [0x0B] = '_',
    [0x0C] = '+', [0x0D] = '|', [0x1A] = '{', [0x1B] = '}', [0x29] = ':', [0x2A] = '"',
    [0x2B] = '~', [0x38] = '<', [0x39] = '>', [0x3A] = '?',
};

[[nodiscard]] char Leargas_RawkeyToAscii(u16 code, u16 qualifier)
{
    if (code & IECODE_UP_PREFIX) {
        return 0; // key release — not a typed character
    }
    if (code >= 0x46) {
        return 0;
    }
    char base = k_base[code];
    if (base == 0) {
        return 0; // non-printable key
    }

    bool shift = (qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
    if (base >= 'a' && base <= 'z') {
        bool caps = (qualifier & IEQUALIFIER_CAPSLOCK) != 0;
        bool upper = shift ^ caps; // CapsLock inverts Shift for letters
        return upper ? (char)(base - 'a' + 'A') : base;
    }
    if (shift && k_shift[code]) {
        return k_shift[code];
    }
    return base;
}

// ---- StringInfo editing ----------------------------------------------------

bool Leargas_String_InsertChar(struct StringInfo *si, char c)
{
    if (!si || !si->Buffer || si->MaxChars <= 1) {
        return false;
    }
    if (si->NumChars >= (WORD)(si->MaxChars - 1)) {
        return false; // buffer full (leave room for the NUL)
    }
    if (si->BufferPos < 0 || si->BufferPos > si->NumChars) {
        return false;
    }
    for (i32 i = si->NumChars; i > si->BufferPos; i--) {
        si->Buffer[i] = si->Buffer[i - 1];
    }
    si->Buffer[si->BufferPos] = (UBYTE)c;
    si->NumChars++;
    si->BufferPos++;
    si->Buffer[si->NumChars] = '\0';
    return true;
}

bool Leargas_String_Backspace(struct StringInfo *si)
{
    if (!si || !si->Buffer || si->BufferPos <= 0) {
        return false;
    }
    for (i32 i = si->BufferPos - 1; i < si->NumChars - 1; i++) {
        si->Buffer[i] = si->Buffer[i + 1];
    }
    si->BufferPos--;
    si->NumChars--;
    si->Buffer[si->NumChars] = '\0';
    return true;
}

// ---- Rendering -------------------------------------------------------------

static DathColor grey(struct DathFramebuffer *fb, u8 v)
{
    if (fb->format == DATH_FMT_RGB565) {
        return Dath_RGB565(v, v, v);
    }
    return Dath_RGB(v, v, v);
}

static DathColor cursor_col(struct DathFramebuffer *fb)
{
    if (fb->format == DATH_FMT_RGB565) {
        return Dath_RGB565(0x30, 0x60, 0xC0);
    }
    return Dath_RGB(0x30, 0x60, 0xC0);
}

void Leargas_StringGadget_Render(struct Window *w, struct Gadget *g)
{
    if (!w || !g) {
        return;
    }
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

    DathColor field = grey(fb, 0xE8); // near-white recessed field
    DathColor border = grey(fb, 0x30);
    DathColor text = grey(fb, 0x00);

    Dath_FillRect(fb, x0, y0, gw, gh, field);
    Dath_DrawRect(fb, x0, y0, gw, gh, border);

    struct StringInfo *si = (struct StringInfo *)g->SpecialInfo;
    if (!si || !si->Buffer) {
        return;
    }

    i32 fw = (i32)dath_font_8x8.width;
    i32 fh = (i32)dath_font_8x8.height;
    i32 tx = x0 + 2;
    i32 ty = y0 + (gh - fh) / 2;
    if (ty < y0 + 1) {
        ty = y0 + 1;
    }

    // Phase 1 renders from the buffer start (DispPos = 0) and clips to
    // the field width by character count; horizontal scroll is Phase 3.
    i32 max_chars = (gw - 4) / fw;
    if (max_chars < 0) {
        max_chars = 0;
    }
    char tmp[256];
    i32 n = 0;
    while (n + 1 < (i32)sizeof(tmp) && n < max_chars && si->Buffer[n] != '\0') {
        tmp[n] = (char)si->Buffer[n];
        n++;
    }
    tmp[n] = '\0';
    if (n > 0) {
        Dath_DrawString(fb, &dath_font_8x8, tx, ty, tmp, text, field);
    }

    // Cursor bar at the edit position — only while this gadget is active.
    if (Leargas_ActiveGadget() == g) {
        i32 cpos = si->BufferPos;
        if (cpos > max_chars) {
            cpos = max_chars;
        }
        i32 cx = tx + cpos * fw;
        if (cx >= x0 + 1 && cx <= x0 + gw - 2) {
            Dath_DrawLine(fb, cx, y0 + 2, cx, y0 + gh - 3, cursor_col(fb));
        }
    }
}

// ---- Key routing -----------------------------------------------------------

// GADGETUP delivery hook — kernel installs Leargas_IDCMP_PostGadgetUp;
// host builds may install a stub. Kept here (a plain function pointer) so
// this file carries no kernel dependency.
static Leargas_GadgetUpFn g_gadgetup_router = nullptr;

void Leargas_SetGadgetRouter(Leargas_GadgetUpFn fn)
{
    g_gadgetup_router = fn;
}

bool Leargas_String_RouteKey(struct LeargasPointer *p, const struct LeargasInputEvent *ev)
{
    if (!ev) {
        return false;
    }
    struct Gadget *g = Leargas_ActiveGadget();
    if (!g || (g->GadgetType & GTYP_GTYPEMASK) != GTYP_STRGADGET) {
        return false; // not editing a string gadget — let LF handle the key
    }

    u16 code = ev->ie_code;
    if (code & IECODE_UP_PREFIX) {
        return true; // swallow key releases while editing
    }

    struct StringInfo *si = (struct StringInfo *)g->SpecialInfo;
    struct Window *w = Leargas_ActiveWindow();

    // Return / keypad Enter — commit: drop edit focus, redraw without the
    // cursor, then post IDCMP_GADGETUP.
    if (code == 0x44 || code == 0x43) {
        if (p) {
            Leargas_Pointer_Hide(p);
        }
        Leargas_SetActiveGadget(nullptr);
        if (w) {
            Leargas_StringGadget_Render(w, g);
        }
        if (p) {
            Leargas_Pointer_Show(p);
        }
        if (g_gadgetup_router && w) {
            (void)g_gadgetup_router(w, g);
        }
        return true;
    }

    bool changed = false;
    if (si) {
        if (code == 0x41) { // Backspace
            changed = Leargas_String_Backspace(si);
        } else {
            char c = Leargas_RawkeyToAscii(code, ev->ie_qualifier);
            if (c) {
                changed = Leargas_String_InsertChar(si, c);
            }
        }
    }

    if (changed) {
        if (p) {
            Leargas_Pointer_Hide(p);
        }
        if (w) {
            Leargas_StringGadget_Render(w, g);
        }
        if (p) {
            Leargas_Pointer_Show(p);
        }
    }
    return true; // an active string gadget consumes every key
}
