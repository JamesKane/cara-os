// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ struct IntuitionBase — the library base of
// intuition.library, the typed view of IntuitionBase.
//
// Phase 1 minimum is the LibNode prefix so user code can dereference
// IntuitionBase to read lib_Version / lib_Revision and so the inline
// stubs in <proto/intuition.h> index the negative-side vec table off it.
// The V36+ public fields past LibNode (ViewLord, ActiveWindow,
// ActiveScreen, FirstScreen, Flags, MouseX/MouseY, Seconds/Micros, …)
// are declared as placeholders; their binding lands as Phase 3 fills
// out intuition.library. v0 keeps them zero-init.
//
// Layout: LibNode at offset 0 is V36+ canonical.

#ifndef INTUITION_INTUITIONBASE_H
#define INTUITION_INTUITIONBASE_H

#include <exec/libraries.h>
#include <exec/types.h>

struct View; // <graphics/view.h>

struct IntuitionBase {
    // ---- V36+ public ABI: LibNode prefix --------------------------------
    struct Library LibNode;

    // ---- V36+ public fields — placeholders, zero-init in v0 -------------
    struct View *ViewLord;
    struct Window *ActiveWindow;
    struct Screen *ActiveScreen;
    struct Screen *FirstScreen;
    ULONG Flags;
    WORD MouseY, MouseX;
    ULONG Seconds, Micros;
};

#endif // INTUITION_INTUITIONBASE_H
