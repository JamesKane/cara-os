// SPDX-License-Identifier: BSD-2-Clause
//
// v36hello — the Phase 3 P0 ABI/toolchain proof (docs/PHASE3.md §4 P0,
// docs/PORTING.md). This translation unit is **verbatim AmigaOS
// Release 2 (V36+) source**: it includes only the API-namespace headers
// (<exec/*>, <intuition/*>, <proto/*>) — no cara/* headers, no inline
// assembly, no CaraOS-specific anything. It is the falsifiable form of
// Phase 3's criterion: a well-written V36 program builds against CaraOS
// headers and runs as a Gleas without source edits.
//
// What it does is the canonical "open a window" idiom:
//   OpenLibrary("intuition.library", 36) -> OpenWindow -> CloseWindow ->
//   CloseLibrary, returning RETURN_OK on success.
//
// The C runtime (libcara) provides _start, bootstraps SysBase, calls
// main(), and turns the return value into the exit status — exactly as
// the Amiga startup + amiga.lib do; none of that is this program's
// source. The kernel-side smoke (test_v36hello.c) stands up a screen,
// spawns this ELF, and asserts the exit status.

#include <exec/libraries.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>

// AmigaDOS return codes (normally from <dos/dos.h>, which is a Phase-3
// L3 header not yet present — spelled out here so P0 stays dependency-
// free; the values are the canonical ones).
#define RETURN_OK 0
#define RETURN_FAIL 20

// The <proto/intuition.h> inline stubs index off this global; the V36
// idiom is that the program sets it from the OpenLibrary return.
struct IntuitionBase *IntuitionBase;

int main(void);

int main(void)
{
    struct Library *ilib = OpenLibrary((STRPTR) "intuition.library", 36);
    if (!ilib) {
        return RETURN_FAIL;
    }
    IntuitionBase = (struct IntuitionBase *)ilib;

    char title[] = "Hello";
    struct NewWindow nw = {
        .LeftEdge = 20,
        .TopEdge = 20,
        .Width = 120,
        .Height = 60,
        .DetailPen = 0,
        .BlockPen = 1,
        .IDCMPFlags = 0,
        .Flags = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET,
        .FirstGadget = nullptr,
        .Title = (UBYTE *)title,
        .Screen = nullptr, // the active (Workbench) screen
    };
    struct Window *win = OpenWindow(&nw);
    if (!win) {
        CloseLibrary(ilib);
        return RETURN_FAIL;
    }

    CloseWindow(win);
    CloseLibrary(ilib);
    return RETURN_OK;
}
