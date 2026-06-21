// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ libraries/gadtools.h — gadtools.library types: the gadget
// kinds, the NewGadget/NewMenu descriptors, the GT*_* attribute tags, and
// GadToolsBase. Verbatim names, kind codes, and tag values are ABI so
// V36+ source compiles unmodified. Design: docs/LEARGAS_GADTOOLS.md.
//
// Source: the canonical <libraries/gadtools.h> in the V36+ Includes and
// the RKM Libraries "GadTools" chapter.

#ifndef LIBRARIES_GADTOOLS_H
#define LIBRARIES_GADTOOLS_H

#include <exec/libraries.h>
#include <exec/types.h>
#include <utility/tagitem.h>

struct TextAttr; // <graphics/text.h>

// gadtools.library's base. The library has no public fields beyond the
// LibNode (it keeps no app-visible state); CaraOS constructs it on the
// shared heap like the other bases.
struct GadToolsBase {
    struct Library LibNode;
};

// ---- Gadget kinds (CreateGadgetA's first argument) ------------------------
// Verbatim values: the kinds index gadtools' internal tables.
enum {
    GENERIC_KIND = 0,
    BUTTON_KIND = 1,
    CHECKBOX_KIND = 2,
    INTEGER_KIND = 3,
    LISTVIEW_KIND = 4,
    MX_KIND = 5,
    NUMBER_KIND = 6,
    CYCLE_KIND = 7,
    PALETTE_KIND = 8,
    SCROLLER_KIND = 9,
    // 10 reserved
    SLIDER_KIND = 11,
    STRING_KIND = 12,
    TEXT_KIND = 13,
    NUM_KINDS = 14,
};

// ---- struct NewGadget -----------------------------------------------------
// The geometry/label/id descriptor passed to CreateGadgetA. ng_VisualInfo
// is a GetVisualInfoA() handle; ng_TextAttr is ignored in v0 (one face).
struct NewGadget {
    WORD ng_LeftEdge, ng_TopEdge;
    WORD ng_Width, ng_Height;
    STRPTR ng_GadgetText;
    struct TextAttr *ng_TextAttr;
    UWORD ng_GadgetID;
    ULONG ng_Flags; // PLACETEXT_* / NG_*
    APTR ng_VisualInfo;
    APTR ng_UserData;
};

// ng_Flags — label placement + behaviour (verbatim).
#define PLACETEXT_LEFT (0x0001)
#define PLACETEXT_RIGHT (0x0002)
#define PLACETEXT_ABOVE (0x0004)
#define PLACETEXT_BELOW (0x0008)
#define PLACETEXT_IN (0x0010)
#define NG_HIGHLABEL (0x0020)

// ---- struct NewMenu (the menu builder descriptor) -------------------------
struct NewMenu {
    UBYTE nm_Type;   // NM_* below
    STRPTR nm_Label; // text, or NM_BARLABEL for a separator
    STRPTR nm_CommKey;
    UWORD nm_Flags;
    LONG nm_MutualExclude;
    APTR nm_UserData;
};

// nm_Type values (verbatim).
#define NM_TITLE 1
#define NM_ITEM 2
#define NM_SUB 3
#define NM_END 0
#define IM_ITEM 0x80                     // item with an Image label
#define IM_SUB 0x81                      // subitem with an Image label
#define NM_BARLABEL ((STRPTR)(uptr) - 1) // separator-bar item

// ---- Common GT*_* attribute tags ------------------------------------------
// The base tag for gadtools attributes (GT_TagBase). Per-kind tags add a
// small offset; v0 declares the ones the active kinds need. Later slices
// extend this set as kinds land.
#define GT_TagBase (TAG_USER + 0x80000)

#define GT_VisualInfo (GT_TagBase + 1)
#define GT_Underscore (GT_TagBase + 2)

#define GTVI_NewWindow (GT_TagBase + 3) // GetVisualInfo source hints
#define GTVI_NWTags (GT_TagBase + 4)

// Button/checkbox/cycle/mx/string/integer/text/number attribute tags.
#define GTCB_Checked (GT_TagBase + 8) // CHECKBOX state (BOOL)
#define GTLV_Labels (GT_TagBase + 16)
#define GTLV_Top (GT_TagBase + 17)
#define GTLV_ReadOnly (GT_TagBase + 18)
#define GTMX_Labels (GT_TagBase + 24)
#define GTMX_Active (GT_TagBase + 25)
#define GTTX_Text (GT_TagBase + 32)
#define GTTX_CopyText (GT_TagBase + 33)
#define GTNM_Number (GT_TagBase + 40)
#define GTCY_Labels (GT_TagBase + 48)
#define GTCY_Active (GT_TagBase + 49)
#define GTPA_Color (GT_TagBase + 56)
#define GTSC_Top (GT_TagBase + 64)
#define GTSC_Total (GT_TagBase + 65)
#define GTSC_Visible (GT_TagBase + 66)
#define GTSL_Min (GT_TagBase + 72)
#define GTSL_Max (GT_TagBase + 73)
#define GTSL_Level (GT_TagBase + 74)
#define GTST_String (GT_TagBase + 80)
#define GTST_MaxChars (GT_TagBase + 81)
#define GTIN_Number (GT_TagBase + 88)
#define GTIN_MaxChars (GT_TagBase + 89)

// DrawBevelBoxA tags.
#define GTBB_Recessed (GT_TagBase + 96)
#define GT_VisualInfoBB (GT_TagBase + 97)

// LayoutMenusA / GT_GetIMsg tags.
#define GTMN_FrontPen (GT_TagBase + 104)
#define GTMN_TextAttr (GT_TagBase + 105)

#endif // LIBRARIES_GADTOOLS_H
