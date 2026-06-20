// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ struct GfxBase — the library base of graphics.library
// (implemented by Dath). The typed view of GfxBase.
//
// L4.1 minimum is the LibNode prefix so user code can dereference
// GfxBase to read lib_Version / lib_Revision and so the inline stubs in
// <proto/graphics.h> index the negative-side vec table off it. The V36+
// public fields past LibNode (the chipset bits — ActiView, copinit, cia,
// blitter queues — are meaningless on CaraOS, which has no chipset) are
// reduced to what the API actually needs: DefaultFont, returned by
// AskFont/used by Text when a RastPort has no font set. They stay
// zero/placeholder until later L4 slices bind them.
//
// Layout: LibNode at offset 0 is V36+ canonical. See docs/DATH_GRAPHICS.md.

#ifndef GRAPHICS_GFXBASE_H
#define GRAPHICS_GFXBASE_H

#include <exec/libraries.h>
#include <exec/types.h>

struct TextFont; // <graphics/text.h>

struct GfxBase {
    // ---- V36+ public ABI: LibNode prefix --------------------------------
    struct Library LibNode;

    // ---- V36+ public fields — placeholders, zero-init in v0 -------------
    // The system default font (bound when L4.5 lands fonts; the Dath 8x8
    // face). RastPorts with no SetFont fall back to this.
    struct TextFont *DefaultFont;
    UWORD DisplayFlags;
};

#endif // GRAPHICS_GFXBASE_H
