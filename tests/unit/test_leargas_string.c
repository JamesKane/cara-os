// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/leargas.h LH — the string Inntin. Covers the
// rawkey→ASCII keymap, StringInfo editing, string-field rendering (incl.
// the cursor), and the router's editing path (active string gadget eats
// keys; Return fires the GADGETUP hook + drops focus). The kernel-only
// IDCMP_GADGETUP MsgPort delivery is covered by KERNEL_TEST(string_gadgetup).

#include <cara/dath.h>
#include <cara/leargas.h>
#include <devices/inputevent.h>
#include <stdio.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_leargas_string: FAIL: %s\n", msg);
    return code;
}

static u32 g_fb_storage[200 * 120];
static u32 g_save_storage[16 * 16];

static void fb_init(struct DathFramebuffer *fb)
{
    fb->base = g_fb_storage;
    fb->width = 200;
    fb->height = 120;
    fb->stride = 200 * 4;
    fb->format = DATH_FMT_RGBA8888;
    fb->bpp = 4;
}

static void save_init(struct DathFramebuffer *save)
{
    save->base = g_save_storage;
    save->width = 16;
    save->height = 16;
    save->stride = 16 * 4;
    save->format = DATH_FMT_RGBA8888;
    save->bpp = 4;
}

static UBYTE g_strbuf[32];

static void si_init(struct StringInfo *si, const char *initial)
{
    *si = (struct StringInfo){ 0 };
    si->Buffer = g_strbuf;
    si->MaxChars = 32;
    i32 n = 0;
    while (initial && initial[n] && n < 31) {
        g_strbuf[n] = (UBYTE)initial[n];
        n++;
    }
    g_strbuf[n] = 0;
    si->NumChars = (WORD)n;
    si->BufferPos = (WORD)n;
}

static bool streq(const UBYTE *a, const char *b)
{
    i32 i = 0;
    while (a[i] && b[i]) {
        if (a[i] != (UBYTE)b[i]) {
            return false;
        }
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int g_gup_count;
static struct Window *g_gup_w;
static struct Gadget *g_gup_g;

static bool stub_gadgetup(struct Window *w, struct Gadget *g)
{
    g_gup_count++;
    g_gup_w = w;
    g_gup_g = g;
    return true;
}

static void post_key(u16 code, u16 qual)
{
    struct LeargasInputEvent ev = {
        .ie_class = IECLASS_RAWKEY,
        .ie_code = code,
        .ie_qualifier = qual,
    };
    (void)Leargas_Input_Post(&ev);
}

int main(void)
{
    // ---- Keymap ------------------------------------------------------------
    if (Leargas_RawkeyToAscii(0x20, 0) != 'a') {
        return fail("rawkey 0x20 unshifted != 'a'", 1);
    }
    if (Leargas_RawkeyToAscii(0x20, IEQUALIFIER_LSHIFT) != 'A') {
        return fail("shift 'a' != 'A'", 2);
    }
    if (Leargas_RawkeyToAscii(0x20, IEQUALIFIER_CAPSLOCK) != 'A') {
        return fail("caps 'a' != 'A'", 3);
    }
    if (Leargas_RawkeyToAscii(0x20, IEQUALIFIER_CAPSLOCK | IEQUALIFIER_LSHIFT) != 'a') {
        return fail("caps+shift 'a' != 'a'", 4);
    }
    if (Leargas_RawkeyToAscii(0x01, 0) != '1' ||
        Leargas_RawkeyToAscii(0x01, IEQUALIFIER_RSHIFT) != '!') {
        return fail("digit/shift-digit wrong", 5);
    }
    if (Leargas_RawkeyToAscii(0x40, 0) != ' ') {
        return fail("space wrong", 6);
    }
    if (Leargas_RawkeyToAscii(0x20 | IECODE_UP_PREFIX, 0) != 0) {
        return fail("key-up decoded to a char", 7);
    }
    if (Leargas_RawkeyToAscii(0x44, 0) != 0 || Leargas_RawkeyToAscii(0x41, 0) != 0) {
        return fail("Return/Backspace decoded as printable", 8);
    }

    // ---- StringInfo editing ------------------------------------------------
    {
        struct StringInfo si;
        si_init(&si, "");
        if (!Leargas_String_InsertChar(&si, 'H') || !Leargas_String_InsertChar(&si, 'i')) {
            return fail("insert failed", 9);
        }
        if (!streq(si.Buffer, "Hi") || si.NumChars != 2 || si.BufferPos != 2) {
            return fail("insert result wrong", 10);
        }
        // Insert in the middle.
        si.BufferPos = 1;
        if (!Leargas_String_InsertChar(&si, 'X') || !streq(si.Buffer, "HXi")) {
            return fail("mid-insert wrong", 11);
        }
        if (si.BufferPos != 2) {
            return fail("mid-insert cursor wrong", 12);
        }
        // Backspace removes the char before the cursor ('X').
        if (!Leargas_String_Backspace(&si) || !streq(si.Buffer, "Hi")) {
            return fail("backspace wrong", 13);
        }
        // Backspace at position 0 is a no-op.
        si.BufferPos = 0;
        if (Leargas_String_Backspace(&si)) {
            return fail("backspace at start should fail", 14);
        }
    }
    {
        // Full-buffer rejection.
        struct StringInfo si;
        si = (struct StringInfo){ 0 };
        static UBYTE small[3];
        si.Buffer = small;
        si.MaxChars = 3; // room for 2 chars + NUL
        if (!Leargas_String_InsertChar(&si, 'a') || !Leargas_String_InsertChar(&si, 'b')) {
            return fail("small insert failed", 15);
        }
        if (Leargas_String_InsertChar(&si, 'c')) {
            return fail("insert past capacity succeeded", 16);
        }
        if (!streq(si.Buffer, "ab")) {
            return fail("full buffer corrupted", 17);
        }
    }

    // ---- Setup screen + window + string gadget for render/route ------------
    Leargas_Input_Reset();
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_Screen_SetActive(nullptr);

    struct DathFramebuffer fb;
    fb_init(&fb);
    struct LeargasScreen screen = { 0 };
    if (Leargas_Screen_InitInPlace(&screen, &fb, "S", 0xFF101020u) != CARA_EOK) {
        return fail("screen init failed", 18);
    }
    Leargas_Screen_SetActive(&screen);

    char wtitle[] = "W";
    struct LeargasWindow w = { 0 };
    struct NewWindow nw = {
        .LeftEdge = 10,
        .TopEdge = 10,
        .Width = 150,
        .Height = 90,
        .Title = (UBYTE *)wtitle,
        .Screen = &screen.pub,
        .Flags = WFLG_DRAGBAR,
        .IDCMPFlags = IDCMP_GADGETUP,
    };
    if (Leargas_Window_InitInPlace(&w, &nw) != CARA_EOK) {
        return fail("window init failed", 19);
    }
    Leargas_Window_LinkToScreen(&w);

    struct StringInfo si;
    si_init(&si, "OK");
    struct Gadget g = {
        .LeftEdge = 20,
        .TopEdge = 40,
        .Width = 100,
        .Height = 14,
        .GadgetType = GTYP_STRGADGET,
        .GadgetID = 42,
        .SpecialInfo = &si,
    };
    Leargas_AddGadget(&w.pub, &g);

    // ---- Render: field, and cursor only when active ------------------------
    // Cursor x = field-left (x0+2) + BufferPos*8. Screen field origin:
    // window (10,10)+gadget (20,40) = (30,50). BufferPos = 2 → cx = 32+16 = 48.
    const u32 field = (u32)Dath_RGB(0xE8, 0xE8, 0xE8);
    const u32 cursor = (u32)Dath_RGB(0x30, 0x60, 0xC0);
    const u32 cx = 48;
    const u32 cy = 50 + 7; // middle-ish of the 14px-tall field
    const u32 cidx = cx + cy * 200;

    Leargas_Gadget_Reset(); // not active → no cursor
    Leargas_Gadget_Render(&w.pub, &g);
    if (g_fb_storage[cidx] != field) {
        return fail("inactive string gadget drew a cursor", 20);
    }
    Leargas_SetActiveGadget(&g); // active → cursor bar
    Leargas_Gadget_Render(&w.pub, &g);
    if (g_fb_storage[cidx] != cursor) {
        return fail("active string gadget missing cursor", 21);
    }

    // ---- Router editing path -----------------------------------------------
    g_gup_count = 0;
    g_gup_w = nullptr;
    g_gup_g = nullptr;
    Leargas_SetGadgetRouter(stub_gadgetup);

    si_init(&si, ""); // start empty; gadget already active
    Leargas_SetActiveGadget(&g);
    Leargas_SetActiveWindow(&w.pub);

    struct DathFramebuffer save;
    save_init(&save);
    struct LeargasPointer p;
    if (Leargas_Pointer_Init(&p, &fb, &save, &leargas_pointer_arrow, 0xFFFFFFFFu, 0xFF000000u, 100,
                             80) != CARA_EOK) {
        return fail("pointer init failed", 22);
    }

    post_key(0x25, IEQUALIFIER_LSHIFT); // 'H'
    post_key(0x17, 0);                  // 'i'
    (void)Leargas_Input_Drain(&p);
    if (!streq(si.Buffer, "Hi") || si.NumChars != 2) {
        return fail("typing didn't fill the buffer", 23);
    }
    if (Leargas_ActiveGadget() != &g) {
        return fail("gadget lost focus mid-edit", 24);
    }

    // Backspace deletes the 'i'.
    post_key(0x41, 0);
    (void)Leargas_Input_Drain(&p);
    if (!streq(si.Buffer, "H")) {
        return fail("backspace via router wrong", 25);
    }

    // Return → GADGETUP hook + drop focus.
    post_key(0x44, 0);
    (void)Leargas_Input_Drain(&p);
    if (g_gup_count != 1 || g_gup_w != &w.pub || g_gup_g != &g) {
        return fail("Return didn't fire the GADGETUP hook", 26);
    }
    if (Leargas_ActiveGadget() != nullptr) {
        return fail("Return didn't drop edit focus", 27);
    }

    // With no active string gadget, String_RouteKey declines the key.
    {
        struct LeargasInputEvent ev = { .ie_class = IECLASS_RAWKEY, .ie_code = 0x20 };
        if (Leargas_String_RouteKey(&p, &ev)) {
            return fail("RouteKey consumed a key with no active gadget", 28);
        }
    }

    Leargas_Input_Reset();
    Leargas_Screen_SetActive(nullptr);
    Leargas_Focus_Reset();
    Leargas_Gadget_Reset();
    Leargas_SetGadgetRouter(nullptr);
    puts("leargas string ok");
    return 0;
}
