// SPDX-License-Identifier: BSD-2-Clause
//
// Clar — the Phase 1 Workbench analogue (docs/PHASE1_CLAR.md). A normal
// U-mode CaraOS Gleas that drives the V36+ intuition.library LVO surface:
// it owns no privilege, the kernel doesn't know it exists, and the user
// can replace it.
//
// This is Tier 1 + the drawer-click hook: Clar opens a window on the
// boot/active Workbench screen, attaches one drawer ("Bosca") rendered
// by Leargas as a BOOLGADGET, then runs the standard V36+ IDCMP loop
// (WaitPort -> GetMsg -> dispatch). Clicking the drawer delivers
// IDCMP_GADGETUP; Tier 2 will open the drawer's child window with a text
// Inntin in response. For now the click is logged and Clar exits, which
// is what the automated smoke (KERNEL_TEST(clar_smoke)) asserts.
//
// Clar can't draw directly — graphics.library is Phase 4 — so every
// pixel (window chrome, the drawer button + label) is rendered by
// Leargas behind the LVOs. Anything the kernel input router must later
// dereference from another task's context (gadgets, the IntuiMessages)
// lives in the SASOS shared heap, so Clar AllocMem's its gadget rather
// than using a .bss global (which is private to Clar's address space).

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

// Window + drawer geometry. Fixed in Phase 1 (Clar can't query the
// screen size — IntuitionBase->ActiveScreen is a v0 placeholder), and
// shared with the smoke test so it can click the drawer.
#define CLAR_WIN_LEFT 10
#define CLAR_WIN_TOP 10
#define CLAR_WIN_WIDTH 160
#define CLAR_WIN_HEIGHT 90
#define CLAR_DRAWER_LEFT 20
#define CLAR_DRAWER_TOP 30
#define CLAR_DRAWER_WIDTH 60
#define CLAR_DRAWER_HEIGHT 20
#define CLAR_DRAWER_ID 1

struct IntuitionBase *IntuitionBase; // referenced by <proto/intuition.h> stubs

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

int main(void);

int main(void)
{
    log_msg(2, "clar", "clar entered");

    struct Library *ilib = OpenLibrary((STRPTR) "intuition.library", 0);
    if (!ilib) {
        return (int)CLAR_EXIT_NO_INTUITION;
    }
    IntuitionBase = (struct IntuitionBase *)ilib;

    // The desktop window: a draggable, activatable window on the active
    // Workbench screen. Requests the IDCMP classes Clar's loop dispatches.
    char title[] = "Clar";
    struct NewWindow nw = {
        .LeftEdge = CLAR_WIN_LEFT,
        .TopEdge = CLAR_WIN_TOP,
        .Width = CLAR_WIN_WIDTH,
        .Height = CLAR_WIN_HEIGHT,
        .DetailPen = 0,
        .BlockPen = 1,
        .IDCMPFlags = IDCMP_GADGETUP | IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY,
        .Flags = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_ACTIVATE,
        .Title = (UBYTE *)title,
        .Screen = nullptr, // active screen
    };
    struct Window *win = OpenWindow(&nw);
    if (!win) {
        CloseLibrary(ilib);
        return (int)CLAR_EXIT_OPENWINDOW_FAILED;
    }

    // The single drawer "Bosca" — a Leargas-rendered boolean gadget.
    // GACT_RELVERIFY so a click posts IDCMP_GADGETUP on release. Allocated
    // in the shared heap (AllocMem) so the kernel input router can
    // dereference it while running in the input task's context.
    struct Gadget *drawer = (struct Gadget *)AllocMem(sizeof(struct Gadget), MEMF_CLEAR);
    if (!drawer) {
        CloseWindow(win);
        CloseLibrary(ilib);
        return (int)CLAR_EXIT_ALLOC_FAILED;
    }
    drawer->LeftEdge = CLAR_DRAWER_LEFT;
    drawer->TopEdge = CLAR_DRAWER_TOP;
    drawer->Width = CLAR_DRAWER_WIDTH;
    drawer->Height = CLAR_DRAWER_HEIGHT;
    drawer->Flags = 0;
    drawer->Activation = GACT_RELVERIFY;
    drawer->GadgetType = GTYP_BOOLGADGET;
    drawer->GadgetID = CLAR_DRAWER_ID;
    AddGadget(win, drawer, (ULONG)~0u); // append

    log_msg(2, "clar", "desktop up; waiting for drawer click");

    // ---- The V36+ IDCMP loop ----------------------------------------------
    int rc = (int)CLAR_EXIT_OK;
    bool running = true;
    while (running) {
        WaitPort(win->UserPort);
        struct IntuiMessage *im;
        while ((im = (struct IntuiMessage *)GetMsg(win->UserPort)) != nullptr) {
            // Copy out what we need before releasing the message. Phase 1
            // has no ReplyMsg for IDCMP yet (docs/PHASE1_LEARGAS LF) — the
            // consumer frees the shared-heap message directly.
            ULONG cls = im->Class;
            UWORD code = im->Code;
            FreeMem(im, sizeof(struct IntuiMessage));

            if (cls == IDCMP_GADGETUP && code == CLAR_DRAWER_ID) {
                // Tier 2 will OpenWindow the drawer's child + a string
                // Inntin here. Tier 1 just proves the click round-trips.
                log_msg(2, "clar", "drawer opened");
                running = false;
            } else if (cls == IDCMP_CLOSEWINDOW) {
                running = false;
            }
        }
    }

    RemoveGadget(win, drawer);
    FreeMem(drawer, sizeof(struct Gadget));
    CloseWindow(win);
    CloseLibrary(ilib);
    log_msg(2, "clar", "clar exit");
    return rc;
}
