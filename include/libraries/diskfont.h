// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <libraries/diskfont.h> — the disk-font directory ABI:
// FontContents (a `.font` index entry), AvailFonts (one enumerated face),
// and their headers, plus the DiskFontHeader on-disk descriptor. Verbatim
// V36+ names/fields so a V36 source program that calls OpenDiskFont /
// AvailFonts compiles unchanged (docs/PRINCIPLES §3.1). On CaraOS the
// actual on-disk font is a Cara font file (a flat bitmap strike, not a 68k
// hunk — docs/DISKFONT.md §2.2); DiskFontHeader.dfh_Segment is therefore
// ABI-only. The tagged variants (TFontContents/TAvailFonts, V44) are
// deferred.

#ifndef LIBRARIES_DISKFONT_H
#define LIBRARIES_DISKFONT_H

#include <exec/libraries.h>
#include <exec/nodes.h>
#include <exec/types.h>
#include <graphics/text.h>

#define MAXFONTPATH 256

// One entry of a FONTS:<name>.font index: a font file + its attributes.
struct FontContents {
    char fc_FileName[MAXFONTPATH];
    UWORD fc_YSize;
    UBYTE fc_Style;
    UBYTE fc_Flags;
};

// The header of a FONTS:<name>.font index file (followed by fch_NumEntries
// FontContents records).
struct FontContentsHeader {
    UWORD fch_FileID;     // FCH_ID
    UWORD fch_NumEntries; // FontContents records that follow
    // struct FontContents fch_FC[fch_NumEntries];
};

#define FCH_ID 0x0f00  // FontContentsHeader (bitmap)
#define TFCH_ID 0x0f02 // tagged FontContents (V44, deferred)
#define OFCH_ID 0x0f03 // outline FontContents (deferred)

#define DFH_ID 0x0f80

// On-disk font descriptor. On classic AmigaOS dfh_Segment is a LoadSeg'd
// hunk; CaraOS loads a flat Cara font file instead, so this is ABI-only.
struct DiskFontHeader {
    struct Node dfh_DF; // ln_Name = the font's name
    UWORD dfh_FileID;   // DFH_ID
    UWORD dfh_Revision;
    LONG dfh_Segment; // unused on CaraOS (no hunk loader)
    char dfh_Name[MAXFONTPATH];
    struct TextFont dfh_TF; // the font itself
};

// One face surfaced by AvailFonts.
struct AvailFonts {
    UWORD af_Type; // AFF_* (where it lives)
    struct TextAttr af_Attr;
};

// The header AvailFonts writes at the front of the caller's buffer
// (followed by afh_NumEntries AvailFonts records).
struct AvailFontsHeader {
    UWORD afh_NumEntries;
    // struct AvailFonts afh_AF[afh_NumEntries];
};

// af_Type / AvailFonts flags (the request flags + the per-entry "where").
#define AFB_MEMORY 0
#define AFF_MEMORY (1 << AFB_MEMORY)
#define AFB_DISK 1
#define AFF_DISK (1 << AFB_DISK)
#define AFB_SCALED 2
#define AFF_SCALED (1 << AFB_SCALED)
#define AFB_BITMAP 3
#define AFF_BITMAP (1 << AFB_BITMAP)
#define AFB_TAGGED 4
#define AFF_TAGGED (1 << AFB_TAGGED)

// diskfont.library's base — no public fields beyond the LibNode (the
// CaraOS convention for these generated libraries, e.g. struct IconBase).
struct DiskfontBase {
    struct Library LibNode;
};

#endif // LIBRARIES_DISKFONT_H
