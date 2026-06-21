// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <intuition/screens.h> — the canonical Screen shape
// every Release-2 program targets.
//
// **Phase 1 layout deviation.** V36+ embeds ViewPort, RastPort,
// BitMap, and Layer_Info BY VALUE inside struct Screen. CaraOS
// Phase 1 carries forward-declared POINTERS to those types
// instead. The field NAMES match V36+ verbatim so future Phase 3
// LVO bodies and userland source target the same identifiers; the
// pointer-vs-embedded shape changes when the graphics types ship
// in Phase 3 / Phase 4 (graphics.library bring-up). Programs that
// dereference `screen->RastPort.foo` (V36+ style) will need a
// trivial `screen->RastPort->foo` fix-up at that transition. No
// Phase 1 source uses the embedded fields directly; Leargas
// internals reach into the `LeargasScreen` brand wrapper for
// kernel state. See docs/PHASE1_LEARGAS.md and
// docs/DRIFT_2026-05.md.
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition,
// intuition/screens.h (Release 2.04 / Revision 36.39).

#ifndef INTUITION_SCREENS_H
#define INTUITION_SCREENS_H

#include <exec/types.h>
#include <graphics/gfx.h>        // PLANEPTR, struct Rectangle, struct BitMap fwd
#include <graphics/layers.h>     // struct Layer, struct Layer_Info fwds
#include <graphics/rastport.h>   // struct RastPort fwd
#include <graphics/text.h>       // struct TextAttr (full)
#include <graphics/view.h>       // struct ViewPort, struct ColorMap fwds
#include <intuition/intuition.h> // struct Window, struct Gadget fwds

// V36+ Flags bits — only the ones Phase 1 / Clar will plausibly read.
// Full set lands with Phase 3 + Phase 4 multi-screen support.
enum : u16 {
    // Screen type. WBENCHSCREEN is the Workbench / Clar screen;
    // CUSTOMSCREEN is anything else.
    WBENCHSCREEN = 0x0001,
    CUSTOMSCREEN = 0x000F,
    SCREENTYPE = 0x000F,

    // V36+ display modes.
    SHOWTITLE = 0x0010,
    BEEPING = 0x0020,
    CUSTOMBITMAP = 0x0040,
    SCREENBEHIND = 0x0080,
    SCREENQUIET = 0x0100,
};

struct Screen {
    struct Screen *NextScreen;
    struct Window *FirstWindow;

    WORD LeftEdge, TopEdge; // top-left in screen coords
    WORD Width, Height;     // pixel extent
    WORD MouseY, MouseX;    // current pointer position
    UWORD Flags;            // SCREENTYPE / SHOWTITLE / etc.

    UBYTE *Title;        // current title bar text (Leargas-owned buffer)
    UBYTE *DefaultTitle; // restored after a window's title takes over

    // V36+ title-bar / border defaults — Leargas fills these from
    // intuition.library defaults at OpenScreen time so client code
    // that lays out windows by reading these gets correct insets.
    BYTE BarHeight;
    BYTE BarVBorder, BarHBorder;
    BYTE MenuVBorder, MenuHBorder;
    BYTE WBorTop, WBorLeft, WBorRight, WBorBottom;

    struct TextAttr *Font; // default font; nullptr in Phase 1

    // V36+ embeds these by-value. Phase 1 carries pointers per the
    // file-banner deviation note. Phase 1 leaves them nullptr —
    // Leargas's own state lives in the brand wrapper.
    struct ViewPort *ViewPort;
    struct RastPort *RastPort;
    struct BitMap *BitMap;
    struct Layer_Info *LayerInfo;

    struct Gadget *FirstGadget; // screen-attached gadgets (depth, etc.)
    UBYTE DetailPen, BlockPen;  // colours for screen chrome
    UWORD SaveColor0;           // restored at CloseScreen

    struct Layer *BarLayer; // title-bar layer; null in Phase 1

    UBYTE *ExtData;
    UBYTE *UserData;
};

// ---- struct NewScreen (V36+) — OpenScreen argument (L5.6) -----------------
//
// v0 screens reuse the boot display framebuffer's format/size (one
// display, no mode database — that's Phase 4 RTG), so the geometry/depth
// fields are accepted but largely advisory.
struct NewScreen {
    WORD LeftEdge, TopEdge, Width, Height, Depth;
    UBYTE DetailPen, BlockPen;
    UWORD ViewModes;
    UWORD Type; // WBENCHSCREEN / CUSTOMSCREEN
    struct TextAttr *Font;
    UBYTE *DefaultTitle;
    struct Gadget *Gadgets;
    struct BitMap *CustomBitMap;
};

// ---- SA_* screen-attribute tags (V36+) — OpenScreenTagList ----------------
//
// SA_Dummy == TAG_USER + 32. v0 honours SA_Left/Top/Width/Height/Depth/
// DetailPen/BlockPen/Title/Type into the NewScreen; the rest are ignored
// (single boot display).
#define SA_Dummy (0x80000000UL + 32)
#define SA_Left (SA_Dummy + 1)
#define SA_Top (SA_Dummy + 2)
#define SA_Width (SA_Dummy + 3)
#define SA_Height (SA_Dummy + 4)
#define SA_Depth (SA_Dummy + 5)
#define SA_DetailPen (SA_Dummy + 6)
#define SA_BlockPen (SA_Dummy + 7)
#define SA_Title (SA_Dummy + 8)
#define SA_Colors (SA_Dummy + 9)
#define SA_ErrorCode (SA_Dummy + 10)
#define SA_Font (SA_Dummy + 11)
#define SA_SysFont (SA_Dummy + 12)
#define SA_Type (SA_Dummy + 13)
#define SA_BitMap (SA_Dummy + 14)
#define SA_PubName (SA_Dummy + 15)
#define SA_DisplayID (SA_Dummy + 18)
#define SA_AutoScroll (SA_Dummy + 30)
#define SA_Pens (SA_Dummy + 31)

// V36+ default chrome metrics — values Leargas_OpenScreen seeds into
// the BarHeight / WBor* fields on Workbench-flavour screens.
constexpr i8 LEARGAS_DEFAULT_BAR_HEIGHT = 11;
constexpr i8 LEARGAS_DEFAULT_BAR_VBORDER = 1;
constexpr i8 LEARGAS_DEFAULT_BAR_HBORDER = 2;
constexpr i8 LEARGAS_DEFAULT_MENU_VBORDER = 1;
constexpr i8 LEARGAS_DEFAULT_MENU_HBORDER = 4;
constexpr i8 LEARGAS_DEFAULT_WBOR_TOP = 2;
constexpr i8 LEARGAS_DEFAULT_WBOR_LEFT = 4;
constexpr i8 LEARGAS_DEFAULT_WBOR_RIGHT = 4;
constexpr i8 LEARGAS_DEFAULT_WBOR_BOTTOM = 2;

#endif // INTUITION_SCREENS_H
