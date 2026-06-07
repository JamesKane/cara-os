// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <intuition/intuition.h> — the public window-system API.
// Phase 1 ships the canonical struct Window field set, plus
// struct NewWindow for OpenWindow, plus the WFLG_* / IDCMP_* /
// WINDOW_* constants needed by epics LD..LH.
//
// **Phase 1 layout deviation** (matches the file-banner note in
// intuition/screens.h): V36+ struct Window embeds RastPort *RPort
// and similar by-value where the spec calls for them; CaraOS Phase 1
// holds them as forward-declared POINTERS instead. The struct
// reserves V36+ field NAMES verbatim so userland source can target
// the same identifiers; the embedded shape lands when graphics.library
// ships in Phase 3 / Phase 4.
//
// Source: AmigaOS RKM Includes & Autodocs 3rd Edition,
// intuition/intuition.h (Release 2.04 / Revision 36.39).

#ifndef INTUITION_INTUITION_H
#define INTUITION_INTUITION_H

#include <exec/ports.h> // struct Message — the IntuiMessage prefix
#include <exec/types.h>

// Forward declarations for types that struct Window references.
// Each gets its own header in <intuition/*.h> as Phase 1 / Phase 3
// epics fill them out (LG → Gadget; Phase 3 → Requester / Menu).
struct Screen;
struct RastPort;
struct BitMap;
struct Layer;
struct TextFont;
struct Image;
struct Window;
struct Gadget;
struct Requester;
struct Menu;
struct MsgPort;
struct IntuiMessage;

// ---- WFLG_* — window Flags bits (V36+ verbatim) ---------------------------

enum : u32 {
    WFLG_SIZEGADGET = 0x00000001u,
    WFLG_DRAGBAR = 0x00000002u,
    WFLG_DEPTHGADGET = 0x00000004u,
    WFLG_CLOSEGADGET = 0x00000008u,
    WFLG_SIZEBRIGHT = 0x00000010u,
    WFLG_SIZEBBOTTOM = 0x00000020u,
    WFLG_REFRESHBITS = 0x000000C0u,
    WFLG_SMART_REFRESH = 0x00000000u,
    WFLG_SIMPLE_REFRESH = 0x00000040u,
    WFLG_SUPER_BITMAP = 0x00000080u,
    WFLG_OTHER_REFRESH = 0x000000C0u,
    WFLG_BACKDROP = 0x00000100u,
    WFLG_REPORTMOUSE = 0x00000200u,
    WFLG_GIMMEZEROZERO = 0x00000400u,
    WFLG_BORDERLESS = 0x00000800u,
    WFLG_ACTIVATE = 0x00001000u,
    WFLG_WINDOWACTIVE = 0x00002000u, // set by Intuition
    WFLG_INREQUEST = 0x00004000u,
    WFLG_MENUSTATE = 0x00008000u,
    WFLG_RMBTRAP = 0x00010000u,
    WFLG_NOCAREREFRESH = 0x00020000u,
    WFLG_NW_EXTENDED = 0x00040000u, // NewWindow → ExtNewWindow
    WFLG_NEWLOOKMENUS = 0x00200000u,
};

// ---- IDCMP_* — IntuiMessage Class flags (V36+ verbatim) -------------------

enum : u32 {
    IDCMP_SIZEVERIFY = 0x00000001u,
    IDCMP_NEWSIZE = 0x00000002u,
    IDCMP_REFRESHWINDOW = 0x00000004u,
    IDCMP_MOUSEBUTTONS = 0x00000008u,
    IDCMP_MOUSEMOVE = 0x00000010u,
    IDCMP_GADGETDOWN = 0x00000020u,
    IDCMP_GADGETUP = 0x00000040u,
    IDCMP_REQSET = 0x00000080u,
    IDCMP_MENUPICK = 0x00000100u,
    IDCMP_CLOSEWINDOW = 0x00000200u,
    IDCMP_RAWKEY = 0x00000400u,
    IDCMP_REQVERIFY = 0x00000800u,
    IDCMP_REQCLEAR = 0x00001000u,
    IDCMP_MENUVERIFY = 0x00002000u,
    IDCMP_NEWPREFS = 0x00004000u,
    IDCMP_DISKINSERTED = 0x00008000u,
    IDCMP_DISKREMOVED = 0x00010000u,
    IDCMP_WBENCHMESSAGE = 0x00020000u,
    IDCMP_ACTIVEWINDOW = 0x00040000u,
    IDCMP_INACTIVEWINDOW = 0x00080000u,
    IDCMP_DELTAMOVE = 0x00100000u,
    IDCMP_VANILLAKEY = 0x00200000u,
    IDCMP_INTUITICKS = 0x00400000u,
    IDCMP_IDCMPUPDATE = 0x00800000u,
    IDCMP_MENUHELP = 0x01000000u,
    IDCMP_CHANGEWINDOW = 0x02000000u,
    IDCMP_GADGETHELP = 0x04000000u,
};

// ---- struct Window (V36+ public field set) ---------------------------------

struct Window {
    struct Window *NextWindow; // for the screen's window list

    WORD LeftEdge, TopEdge;
    WORD Width, Height;
    WORD MouseY, MouseX; // pointer position relative to window origin
    WORD MinWidth, MinHeight;
    UWORD MaxWidth, MaxHeight;

    ULONG Flags; // WFLG_*

    struct Menu *MenuStrip;
    UBYTE *Title;

    struct Requester *FirstRequest;
    struct Requester *DMRequest;
    WORD ReqCount;

    struct Screen *WScreen; // owning screen

    // V36+ embeds RastPort + BorderRPort by value. Phase 1 carries
    // pointers per the file-banner deviation; Leargas's renderer
    // reaches into the brand wrapper for the actual surface.
    struct RastPort *RPort;
    BYTE BorderLeft, BorderTop, BorderRight, BorderBottom;
    struct RastPort *BorderRPort;

    struct Gadget *FirstGadget;

    struct Window *Parent, *Descendant;

    UWORD *Pointer; // custom mouse pointer image
    BYTE PtrHeight, PtrWidth;
    WORD XOffset, YOffset;

    ULONG IDCMPFlags;           // class bits client wants
    struct MsgPort *UserPort;   // client receives IntuiMessages here
    struct MsgPort *WindowPort; // Intuition private
    struct IntuiMessage *MessageKey;

    UBYTE DetailPen, BlockPen;
    struct Image *CheckMark;
    UBYTE *ScreenTitle; // shown in screen title bar when active

    WORD GZZMouseX, GZZMouseY;
    WORD GZZWidth, GZZHeight;

    UBYTE *ExtData;
    BYTE *UserData;

    struct Layer *WLayer; // null in Phase 1 (no Layers yet)
    struct TextFont *IFont;
    ULONG MoreFlags;
};

// ---- struct NewWindow (argument to Leargas_OpenWindow) -------------------

struct NewWindow {
    WORD LeftEdge, TopEdge;
    WORD Width, Height;
    UBYTE DetailPen, BlockPen;
    ULONG IDCMPFlags;
    ULONG Flags;
    struct Gadget *FirstGadget;
    struct Image *CheckMark;
    UBYTE *Title;
    struct Screen *Screen; // target screen; nullptr ≡ active screen
    struct BitMap *BitMap;
    WORD MinWidth, MinHeight;
    UWORD MaxWidth, MaxHeight;
    UWORD Type; // WBENCHSCREEN / CUSTOMSCREEN, mirroring the screen
};

// ---- struct IntuiMessage (V36+ public field set) --------------------------
//
// What a program reads from its window's UserPort. The ExecMessage
// header makes it a first-class exec Message, so GetMsg / ReplyMsg /
// WaitPort operate on it unchanged. Class is an IDCMP_* flag; Code /
// Qualifier carry the raw event; MouseX / MouseY are window-relative
// at the time of the event; Seconds / Micros are the timestamp.

struct IntuiMessage {
    struct Message ExecMessage; // exec Message prefix — mn_Node etc.

    ULONG Class;     // IDCMP_* — the message class
    UWORD Code;      // event sub-code (rawkey number for IDCMP_RAWKEY)
    UWORD Qualifier; // IEQUALIFIER_* modifier bitmap
    APTR IAddress;   // class-specific (gadget / etc.); nullptr for RAWKEY

    WORD MouseX, MouseY;   // pointer position, relative to the window
    ULONG Seconds, Micros; // event timestamp

    struct Window *IDCMPWindow;       // the window this message is for
    struct IntuiMessage *SpecialLink; // Intuition-private chaining
};

// ---- Phase 1 default chrome metrics ---------------------------------------
//
// Default border insets when a window has WFLG_DRAGBAR (drag bar at
// top) and ordinary side borders. Title bar height matches the
// screen's BarHeight default; side borders are 1 pixel.

constexpr i8 LEARGAS_WINDOW_DEFAULT_BORDER_TOP = 11; // matches BarHeight
constexpr i8 LEARGAS_WINDOW_DEFAULT_BORDER_BOTTOM = 1;
constexpr i8 LEARGAS_WINDOW_DEFAULT_BORDER_LEFT = 1;
constexpr i8 LEARGAS_WINDOW_DEFAULT_BORDER_RIGHT = 1;

#endif // INTUITION_INTUITION_H
