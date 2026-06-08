// SPDX-License-Identifier: BSD-2-Clause
//
// Clar — the Phase 1 Workbench analogue (docs/PHASE1_CLAR.md). A normal
// U-mode CaraOS Gleas driving the V36+ intuition.library LVO surface;
// it owns no privilege and the kernel doesn't know it exists.
//
// Tiers 1-3: Clar opens a desktop window on the active Workbench screen
// with one drawer ("Bosca", a Leargas-rendered BOOLGADGET). Clicking the
// drawer opens its child window holding a string Inntin (text gadget),
// activated for input. Typing edits the Inntin (Leargas handles the keys
// + rendering); Return delivers IDCMP_GADGETUP, on which Clar reads the
// buffer and logs what was typed — the Phase 1 success criterion. The
// child's close gadget (IDCMP_CLOSEWINDOW) closes the drawer; the
// desktop's close gadget quits Clar.
//
// Clar can't draw directly (graphics.library is Phase 4): Leargas renders
// all chrome/gadgets behind the LVOs. Anything the kernel input router
// dereferences from another task's context (gadgets, StringInfo, buffer,
// IntuiMessages) lives in the SASOS shared heap, so Clar AllocMem's it.
//
// Multiple windows ⇒ multiple IDCMP UserPorts; Clar Wait()s on the union
// of their signal bits (mp_SigBit) and drains each. The child port is
// drained first so a typed entry is always processed before a quit that
// might arrive in the same wakeup.

#include <cara/sysno.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#define CLAR_EXIT_OK 0xC1A8
#define CLAR_EXIT_NO_INTUITION 0xBAD1
#define CLAR_EXIT_OPENWINDOW_FAILED 0xBAD2
#define CLAR_EXIT_ALLOC_FAILED 0xBAD3
#define CLAR_EXIT_NO_INPUT 0xBAD4 // quit without ever typing into the Inntin

// Desktop window + drawer geometry. Fixed in Phase 1 (Clar can't query
// the screen size) and shared with the smoke test so it can click them.
#define CLAR_WIN_LEFT 10
#define CLAR_WIN_TOP 10
#define CLAR_WIN_WIDTH 160
#define CLAR_WIN_HEIGHT 90
#define CLAR_DRAWER_LEFT 20
#define CLAR_DRAWER_TOP 30
#define CLAR_DRAWER_WIDTH 60
#define CLAR_DRAWER_HEIGHT 20
#define CLAR_DRAWER_ID 1
#define CLAR_INNTIN_ID 2
#define CLAR_INNTIN_BUF 64

struct IntuitionBase *IntuitionBase; // referenced by <proto/intuition.h> stubs

// Child "Bosca" window + its Inntin. File-scope so the open/close helpers
// and the main loop share them.
static struct Window *g_child;
static struct Gadget *g_inntin;
static struct StringInfo *g_si;
static UBYTE *g_buf;

static void log_msg(int level, const char *tag, const char *msg)
{
    long len = 0;
    while (msg[len]) {
        len++;
    }
    register long a0 __asm__("a0") = level;
    register long a1 __asm__("a1") = (long)tag;
    register long a2 __asm__("a2") = (long)msg;
    register long a3 __asm__("a3") = len;
    register long a7 __asm__("a7") = SYS_LOG_WRITE;
    __asm__ volatile("ecall" ::"r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
}

// Open the drawer's child window with a string Inntin and activate it.
// No-op if already open. Returns true on success.
static bool open_drawer(void)
{
    if (g_child) {
        return true;
    }
    g_buf = (UBYTE *)AllocMem(CLAR_INNTIN_BUF, MEMF_CLEAR);
    g_si = (struct StringInfo *)AllocMem(sizeof(struct StringInfo), MEMF_CLEAR);
    g_inntin = (struct Gadget *)AllocMem(sizeof(struct Gadget), MEMF_CLEAR);
    if (!g_buf || !g_si || !g_inntin) {
        return false;
    }
    g_si->Buffer = g_buf;
    g_si->MaxChars = CLAR_INNTIN_BUF;
    g_si->BufferPos = 0;

    g_inntin->LeftEdge = 10;
    g_inntin->TopEdge = 28;
    g_inntin->Width = 160;
    g_inntin->Height = 16;
    g_inntin->Activation = GACT_RELVERIFY;
    g_inntin->GadgetType = GTYP_STRGADGET;
    g_inntin->SpecialInfo = g_si;
    g_inntin->GadgetID = CLAR_INNTIN_ID;

    char btitle[] = "Bosca";
    struct NewWindow cnw = {
        .LeftEdge = 30,
        .TopEdge = 55,
        .Width = 190,
        .Height = 60,
        .DetailPen = 0,
        .BlockPen = 1,
        .IDCMPFlags = IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_RAWKEY,
        .Flags = WFLG_DRAGBAR | WFLG_CLOSEGADGET | WFLG_ACTIVATE,
        .Title = (UBYTE *)btitle,
        .Screen = nullptr,
    };
    g_child = OpenWindow(&cnw);
    if (!g_child) {
        return false;
    }
    AddGadget(g_child, g_inntin, (ULONG)~0u);
    ActivateGadget(g_inntin, g_child, nullptr); // ready to type immediately
    return true;
}

static void close_drawer(void)
{
    if (!g_child) {
        return;
    }
    RemoveGadget(g_child, g_inntin);
    CloseWindow(g_child);
    FreeMem(g_buf, CLAR_INNTIN_BUF);
    FreeMem(g_si, sizeof(struct StringInfo));
    FreeMem(g_inntin, sizeof(struct Gadget));
    g_child = nullptr;
    g_inntin = nullptr;
    g_si = nullptr;
    g_buf = nullptr;
}

int main(void);

int main(void)
{
    log_msg(2, "clar", "clar entered");

    struct Library *ilib = OpenLibrary((STRPTR) "intuition.library", 0);
    if (!ilib) {
        return (int)CLAR_EXIT_NO_INTUITION;
    }
    IntuitionBase = (struct IntuitionBase *)ilib;

    char title[] = "Clar";
    struct NewWindow nw = {
        .LeftEdge = CLAR_WIN_LEFT,
        .TopEdge = CLAR_WIN_TOP,
        .Width = CLAR_WIN_WIDTH,
        .Height = CLAR_WIN_HEIGHT,
        .DetailPen = 0,
        .BlockPen = 1,
        .IDCMPFlags = IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY,
        .Flags = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_ACTIVATE,
        .Title = (UBYTE *)title,
        .Screen = nullptr, // active screen
    };
    struct Window *desktop = OpenWindow(&nw);
    if (!desktop) {
        CloseLibrary(ilib);
        return (int)CLAR_EXIT_OPENWINDOW_FAILED;
    }

    struct Gadget *drawer = (struct Gadget *)AllocMem(sizeof(struct Gadget), MEMF_CLEAR);
    if (!drawer) {
        CloseWindow(desktop);
        CloseLibrary(ilib);
        return (int)CLAR_EXIT_ALLOC_FAILED;
    }
    drawer->LeftEdge = CLAR_DRAWER_LEFT;
    drawer->TopEdge = CLAR_DRAWER_TOP;
    drawer->Width = CLAR_DRAWER_WIDTH;
    drawer->Height = CLAR_DRAWER_HEIGHT;
    drawer->Activation = GACT_RELVERIFY;
    drawer->GadgetType = GTYP_BOOLGADGET;
    drawer->GadgetID = CLAR_DRAWER_ID;
    AddGadget(desktop, drawer, (ULONG)~0u);

    log_msg(2, "clar", "desktop up; click the drawer");

    // ---- The V36+ IDCMP loop over both windows' UserPorts ------------------
    int rc = (int)CLAR_EXIT_NO_INPUT;
    bool typed_ok = false;
    bool running = true;
    while (running) {
        ULONG mask = 1u << desktop->UserPort->mp_SigBit;
        if (g_child) {
            mask |= 1u << g_child->UserPort->mp_SigBit;
        }
        (void)Wait(mask);

        // Drain the child port first: a typed Inntin entry must be
        // processed before any quit that arrives in the same wakeup.
        bool close_child = false;
        if (g_child) {
            struct IntuiMessage *im;
            while ((im = (struct IntuiMessage *)GetMsg(g_child->UserPort)) != nullptr) {
                ULONG cls = im->Class;
                UWORD code = im->Code;
                FreeMem(im, sizeof(struct IntuiMessage));
                if (cls == IDCMP_GADGETUP && code == CLAR_INNTIN_ID) {
                    // The user pressed Return in the Inntin. Read what
                    // they typed (the buffer is shared memory we own).
                    char line[16 + CLAR_INNTIN_BUF];
                    int n = 0;
                    const char *pfx = "gadget got '";
                    while (pfx[n]) {
                        line[n] = pfx[n];
                        n++;
                    }
                    for (int i = 0; g_buf[i] && n < (int)sizeof(line) - 2; i++) {
                        line[n++] = (char)g_buf[i];
                    }
                    line[n++] = '\'';
                    line[n] = 0;
                    log_msg(2, "clar", line);
                    if (g_buf[0]) {
                        typed_ok = true;
                    }
                    // Re-activate so further keystrokes keep reaching it.
                    ActivateGadget(g_inntin, g_child, nullptr);
                } else if (cls == IDCMP_CLOSEWINDOW) {
                    close_child = true;
                }
            }
        }
        if (close_child) {
            close_drawer();
        }

        struct IntuiMessage *im;
        while ((im = (struct IntuiMessage *)GetMsg(desktop->UserPort)) != nullptr) {
            ULONG cls = im->Class;
            UWORD code = im->Code;
            FreeMem(im, sizeof(struct IntuiMessage));
            if (cls == IDCMP_GADGETUP && code == CLAR_DRAWER_ID) {
                if (!open_drawer()) {
                    running = false; // alloc/open failed — bail
                }
            } else if (cls == IDCMP_CLOSEWINDOW) {
                running = false; // closing the desktop quits Clar
            }
        }
    }

    if (typed_ok) {
        rc = (int)CLAR_EXIT_OK;
    }

    close_drawer();
    RemoveGadget(desktop, drawer);
    FreeMem(drawer, sizeof(struct Gadget));
    CloseWindow(desktop);
    CloseLibrary(ilib);
    log_msg(2, "clar", "clar exit");
    return rc;
}
