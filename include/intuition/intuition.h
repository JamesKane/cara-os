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
struct IntuiText;
struct StringInfo; // SpecialInfo for GTYP_STRGADGET (filled out in LH)
struct TextAttr;
struct KeyMap; // alternate keymap (Phase 3 keymap.library); null = default

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

// ---- IDCMP_MOUSEBUTTONS Code values (V36+ verbatim) -----------------------
//
// The Code field of an IDCMP_MOUSEBUTTONS IntuiMessage. SELECT is the
// left button, MENU the right; the high bit (IECODE_UP_PREFIX, 0x80)
// distinguishes release from press — so these are the raw IECODE button
// values surfaced to the application unchanged.
enum : u16 {
    SELECTDOWN = 0x68,
    SELECTUP = 0xe8,
    MENUDOWN = 0x69,
    MENUUP = 0xe9,
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

// ---- WA_* window-attribute tags (V36+) — OpenWindowTagList ----------------
//
// The tag idiom for OpenWindowTagList/OpenWindowTags (L5.1). Values are
// the canonical V36+ numbers (WA_Dummy == TAG_USER + 99). The opener
// builds a NewWindow from these (the boolean WA_* map to WFLG_* flags).
#define WA_Dummy (0x80000000UL + 99)
#define WA_Left (WA_Dummy + 0x01)
#define WA_Top (WA_Dummy + 0x02)
#define WA_Width (WA_Dummy + 0x03)
#define WA_Height (WA_Dummy + 0x04)
#define WA_DetailPen (WA_Dummy + 0x05)
#define WA_BlockPen (WA_Dummy + 0x06)
#define WA_IDCMP (WA_Dummy + 0x07)
#define WA_Flags (WA_Dummy + 0x08)
#define WA_Gadgets (WA_Dummy + 0x09)
#define WA_Checkmark (WA_Dummy + 0x0A)
#define WA_Title (WA_Dummy + 0x0B)
#define WA_ScreenTitle (WA_Dummy + 0x0C)
#define WA_CustomScreen (WA_Dummy + 0x0D)
#define WA_SuperBitMap (WA_Dummy + 0x0E)
#define WA_MinWidth (WA_Dummy + 0x0F)
#define WA_MinHeight (WA_Dummy + 0x10)
#define WA_MaxWidth (WA_Dummy + 0x11)
#define WA_MaxHeight (WA_Dummy + 0x12)
#define WA_InnerWidth (WA_Dummy + 0x13)
#define WA_InnerHeight (WA_Dummy + 0x14)
#define WA_SizeGadget (WA_Dummy + 0x1E)
#define WA_DragBar (WA_Dummy + 0x1F)
#define WA_DepthGadget (WA_Dummy + 0x20)
#define WA_CloseGadget (WA_Dummy + 0x21)
#define WA_Backdrop (WA_Dummy + 0x22)
#define WA_ReportMouse (WA_Dummy + 0x23)
#define WA_Borderless (WA_Dummy + 0x25)
#define WA_Activate (WA_Dummy + 0x26)
#define WA_RMBTrap (WA_Dummy + 0x27)
#define WA_GimmeZeroZero (WA_Dummy + 0x2D)

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

// ---- GTYP_* — Gadget GadgetType (V36+ verbatim) ---------------------------
//
// Low 3 bits (GTYP_GTYPEMASK) select the gadget kind; the high bits flag
// system / screen / requester ownership. Phase 1 LG implements the
// BOOLGADGET face + hit/select; STRGADGET interaction lands in LH.

enum : u16 {
    GTYP_GADGETTYPE = 0xFC00, // reserved system-type bits
    GTYP_SYSGADGET = 0x8000,
    GTYP_SCRGADGET = 0x4000,
    GTYP_GZZGADGET = 0x2000,
    GTYP_REQGADGET = 0x1000,
    GTYP_SIZING = 0x0010,
    GTYP_WDRAGGING = 0x0020,
    GTYP_SDRAGGING = 0x0030,
    GTYP_WUPFRONT = 0x0040,
    GTYP_SUPFRONT = 0x0050,
    GTYP_WDOWNBACK = 0x0060,
    GTYP_SDOWNBACK = 0x0070,
    GTYP_CLOSE = 0x0080,

    GTYP_BOOLGADGET = 0x0001,
    GTYP_GADGET0002 = 0x0002,
    GTYP_PROPGADGET = 0x0003,
    GTYP_STRGADGET = 0x0004,
    GTYP_CUSTOMGADGET = 0x0005,
    GTYP_GTYPEMASK = 0x0007,
};

// ---- GFLG_* — Gadget Flags (V36+ verbatim) --------------------------------

enum : u16 {
    GFLG_GADGHCOMP = 0x0000, // highlight by complementing
    GFLG_GADGHBOX = 0x0001,  // highlight by drawing a box
    GFLG_GADGHIMAGE = 0x0002,
    GFLG_GADGHNONE = 0x0003,
    GFLG_GADGHIGHBITS = 0x0003,
    GFLG_GADGIMAGE = 0x0004, // GadgetRender points to an Image, not a Border
    GFLG_RELBOTTOM = 0x0008, // TopEdge is relative to the window bottom
    GFLG_RELRIGHT = 0x0010,  // LeftEdge is relative to the window right
    GFLG_RELWIDTH = 0x0020,
    GFLG_RELHEIGHT = 0x0040,
    GFLG_SELECTED = 0x0080, // currently selected (pressed)
    GFLG_DISABLED = 0x0100, // ghosted; ignores input
    GFLG_STRINGEXTEND = 0x0400,
};

// ---- GACT_* — Gadget Activation (V36+ verbatim) ---------------------------

enum : u16 {
    GACT_RELVERIFY = 0x0001,   // post IDCMP_GADGETUP only if released over the gadget
    GACT_IMMEDIATE = 0x0002,   // post IDCMP_GADGETDOWN on select
    GACT_ENDGADGET = 0x0004,   // (requesters) selecting ends the requester
    GACT_FOLLOWMOUSE = 0x0008, // report mouse moves while selected
    GACT_RIGHTBORDER = 0x0010,
    GACT_LEFTBORDER = 0x0020,
    GACT_TOPBORDER = 0x0040,
    GACT_BOTTOMBORDER = 0x0080,
    GACT_TOGGLESELECT = 0x0100, // selection toggles rather than momentary
    GACT_BOOLEXTEND = 0x2000,
    GACT_STRINGLEFT = 0x0000, // string justification (LH)
    GACT_STRINGCENTER = 0x0200,
    GACT_STRINGRIGHT = 0x0400,
    GACT_LONGINT = 0x0800,
    GACT_ALTKEYMAP = 0x1000,
};

// ---- struct IntuiText (V36+) — a gadget / window label --------------------

struct IntuiText {
    UBYTE FrontPen, BackPen; // pen numbers (palette indices)
    UBYTE DrawMode;          // JAM1 / JAM2 / COMPLEMENT
    WORD LeftEdge, TopEdge;  // offset from the host object's origin
    struct TextAttr *ITextFont;
    UBYTE *IText;               // NUL-terminated text
    struct IntuiText *NextText; // chained text segments
};

// ---- struct Gadget (V36+ public field set) --------------------------------
//
// The app allocates these and chains them via NextGadget; a window's
// FirstGadget points at the head. Coordinates are window-relative.

struct Gadget {
    struct Gadget *NextGadget;

    WORD LeftEdge, TopEdge;
    WORD Width, Height;

    UWORD Flags;      // GFLG_*
    UWORD Activation; // GACT_*
    UWORD GadgetType; // GTYP_*

    APTR GadgetRender; // Image / Border for the rendered look (Phase 3+)
    APTR SelectRender; // alternate imagery for the selected state

    struct IntuiText *GadgetText; // label

    LONG MutualExclude; // bit mask of sibling gadgets to deselect

    APTR SpecialInfo; // StringInfo * for GTYP_STRGADGET (LH), etc.

    UWORD GadgetID; // app-assigned id, echoed in IDCMP_GADGETUP
    APTR UserData;
};

// ---- struct StringInfo (V36+) — SpecialInfo for GTYP_STRGADGET -------------
//
// A GTYP_STRGADGET's SpecialInfo points here. The client owns Buffer
// (MaxChars bytes, NUL-terminated); Leargas edits it in place. The
// Intuition-private tail (UndoPos onward) is filled / used by the
// editor. Phase 1 LH uses Buffer / MaxChars / BufferPos / NumChars and
// renders from DispPos = 0 (no horizontal scroll yet).

struct StringInfo {
    UBYTE *Buffer;     // the string the user edits (NUL-terminated)
    UBYTE *UndoBuffer; // optional undo scratch (MaxChars bytes)
    WORD BufferPos;    // cursor position within Buffer
    WORD MaxChars;     // Buffer size in bytes, including the NUL
    WORD DispPos;      // first displayed character

    // Intuition-private:
    WORD UndoPos;  // cursor position within UndoBuffer
    WORD NumChars; // current length of Buffer (excluding NUL)
    WORD DispCount;
    WORD CLeft, CTop;         // RastPort offset of the container
    struct Layer *LayerPtr;   // null in Phase 1
    LONG LongInt;             // GACT_LONGINT integer result
    struct KeyMap *AltKeyMap; // alternate keymap; null = default (Phase 3)
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
