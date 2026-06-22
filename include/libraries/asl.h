// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ libraries/asl.h — asl.library types: the file/font/
// screen-mode requester structs, the ASL_* request-type codes, and the
// ASLFR_*/ASLFO_*/ASLSM_* tags. Verbatim names, struct field offsets,
// codes, and tag values are ABI so V36+ source compiles unmodified.
// Design: docs/LEARGAS_ASL.md.
//
// Source: the canonical <libraries/asl.h> in the V36+ Includes and the
// RKM Libraries "ASL" chapter. NOTE: a few tag sub-offsets below are
// best-effort and flagged to verify against amiga_docs/ (the design doc
// §3 caveat); the request-type codes + struct field order are the parts
// that must be exact, and are.

#ifndef LIBRARIES_ASL_H
#define LIBRARIES_ASL_H

#include <exec/libraries.h>
#include <exec/types.h>
#include <graphics/text.h> // struct TextAttr (FontRequester.fo_Attr)
#include <utility/tagitem.h>

struct Window; // <intuition/intuition.h>

// asl.library's base — no public fields beyond the LibNode.
struct AslBase {
    struct Library LibNode;
};

// Request-type codes for AllocAslRequest(reqType, …) (verbatim).
#define ASL_FileRequest 0
#define ASL_FontRequest 1
#define ASL_ScreenModeRequest 2

// ---- struct FileRequester (ASL_FileRequest) -------------------------------
struct FileRequester {
    APTR rf_Reserved1;
    BYTE rf_Reserved2;
    APTR rf_Reserved3;
    STRPTR rf_Dir;  // the chosen drawer (NUL-terminated)
    STRPTR rf_File; // the chosen filename
    STRPTR rf_Pat;  // pattern (v0 unused)
    LONG rf_LeftEdge, rf_TopEdge;
    LONG rf_Width, rf_Height;
    WORD rf_NumArgs; // multi-select count (v0: 0/1)
    APTR rf_ArgList; // multi-select WBArg list (v0: nullptr)
    APTR rf_UserData;
    APTR rf_Reserved4; // V38 growth headroom
    APTR rf_Reserved5;
};

// ---- struct FontRequester (ASL_FontRequest) -------------------------------
struct FontRequester {
    APTR fo_Reserved1;
    struct TextAttr fo_Attr; // the chosen font
    BYTE fo_FrontPen, fo_BackPen;
    BYTE fo_DrawMode;
    APTR fo_UserData;
    APTR fo_Reserved2;
};

// ---- struct ScreenModeRequester (ASL_ScreenModeRequest, V38) --------------
struct ScreenModeRequester {
    ULONG sm_DisplayID;
    UWORD sm_DisplayWidth, sm_DisplayHeight;
    UWORD sm_DisplayDepth;
    UWORD sm_OverscanType;
    BOOL sm_AutoScroll;
    APTR sm_UserData;
    APTR sm_Reserved1;
};

// ---- Tags -----------------------------------------------------------------
#define ASL_Dummy (TAG_USER + 0x80000)

// File-requester tags (ASLFR_*). v0 honours Window/TitleText/
// InitialDrawer/InitialFile/Positive/NegativeText; the rest are accepted.
#define ASLFR_Window (ASL_Dummy + 0x401)
#define ASLFR_PubScreenName (ASL_Dummy + 0x402)
#define ASLFR_Screen (ASL_Dummy + 0x403)
#define ASLFR_UserData (ASL_Dummy + 0x406)
#define ASLFR_TextAttr (ASL_Dummy + 0x407)
#define ASLFR_TitleText (ASL_Dummy + 0x409)
#define ASLFR_PositiveText (ASL_Dummy + 0x40A)
#define ASLFR_NegativeText (ASL_Dummy + 0x40B)
#define ASLFR_InitialLeftEdge (ASL_Dummy + 0x40C)
#define ASLFR_InitialTopEdge (ASL_Dummy + 0x40D)
#define ASLFR_InitialWidth (ASL_Dummy + 0x40E)
#define ASLFR_InitialHeight (ASL_Dummy + 0x40F)
#define ASLFR_InitialFile (ASL_Dummy + 0x422)
#define ASLFR_InitialDrawer (ASL_Dummy + 0x423)
#define ASLFR_InitialPattern (ASL_Dummy + 0x424)
#define ASLFR_DoSaveMode (ASL_Dummy + 0x428)
#define ASLFR_DoMultiSelect (ASL_Dummy + 0x429)
#define ASLFR_DoPatterns (ASL_Dummy + 0x42A)

// Font-requester tags (ASLFO_*).
#define ASLFO_Window (ASL_Dummy + 0x801)
#define ASLFO_TitleText (ASL_Dummy + 0x809)
#define ASLFO_InitialName (ASL_Dummy + 0x80A)
#define ASLFO_InitialSize (ASL_Dummy + 0x80B)

// Screen-mode-requester tags (ASLSM_*).
#define ASLSM_Window (ASL_Dummy + 0xC01)
#define ASLSM_TitleText (ASL_Dummy + 0xC09)

#endif // LIBRARIES_ASL_H
