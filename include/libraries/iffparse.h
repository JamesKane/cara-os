// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ libraries/iffparse.h — IFF (Interchange File Format) types:
// the IFFHandle, the parse context stack (ContextNode), the gathered
// property / collection structs, the error codes, parse modes, and the
// standard chunk IDs. Verbatim names, struct field offsets, codes, and
// IDs are ABI so V36+ source compiles unmodified. Design: docs/IFFPARSE.md.
//
// Source: the canonical <libraries/iffparse.h> in the V36+ Includes and
// the RKM "IFFParse" chapter.

#ifndef LIBRARIES_IFFPARSE_H
#define LIBRARIES_IFFPARSE_H

#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/types.h>

// iffparse.library's base — no public fields beyond the LibNode.
struct IFFParseBase {
    struct Library LibNode;
};

// ---- struct IFFHandle -----------------------------------------------------
// iff_Stream is the client's stream identifier — for InitIFFasDOS it is a
// dos FileHandle (BPTR). Pointer-width (IPTR, not the classic 32-bit
// ULONG) so it holds a 64-bit BPTR on CaraOS.
struct IFFHandle {
    IPTR iff_Stream; // client stream id (dos FileHandle for InitIFFasDOS)
    ULONG iff_Flags; // IFFF_*
    LONG iff_Depth;  // current context-stack depth
};

// ---- struct ContextNode ---------------------------------------------------
// One per open scope (the FORM/LIST/CAT and each chunk being parsed).
struct ContextNode {
    struct MinNode cn_Node;
    LONG cn_Type; // the containing FORM/LIST type (or chunk's group type)
    LONG cn_ID;   // this chunk's id
    LONG cn_Size; // this chunk's data size
    LONG cn_Scan; // bytes of cn_Size already consumed
};

// ---- gathered data --------------------------------------------------------
struct StoredProperty {
    LONG sp_Size;     // size of the gathered prop chunk
    UBYTE sp_Data[1]; // the chunk bytes (sp_Size of them)
};

struct CollectionItem {
    struct CollectionItem *ci_Next; // next item (most-recent-first)
    LONG ci_Size;
    UBYTE *ci_Data;
};

struct LocalContextItem {
    struct MinNode lci_Node;
    LONG lci_ID;
    LONG lci_Type;
    LONG lci_Ident;
};

// ---- OpenIFF / iff_Flags --------------------------------------------------
#define IFFF_READ 0L  // open for reading
#define IFFF_WRITE 1L // open for writing
#define IFFF_RWBITS (IFFF_READ | IFFF_WRITE)
#define IFFF_FSEEK (1L << 1) // forward seeks possible
#define IFFF_RSEEK (1L << 2) // random seeks possible

// ---- ParseIFF control modes -----------------------------------------------
#define IFFPARSE_SCAN 0    // step until a StopChunk
#define IFFPARSE_STEP 1    // one step (enter/leave a chunk)
#define IFFPARSE_RAWSTEP 2 // like STEP, no prop/collection gathering

// ---- error / status codes (0 = success) -----------------------------------
#define IFFERR_EOF (-1L)     // end of the top-level context
#define IFFERR_EOC (-2L)     // end of the current context
#define IFFERR_NOSCOPE (-3L) // no valid scope for the operation
#define IFFERR_NOMEM (-4L)   // out of memory
#define IFFERR_READ (-5L)    // stream read error
#define IFFERR_WRITE (-6L)   // stream write error
#define IFFERR_SEEK (-7L)    // stream seek error
#define IFFERR_MANGLED (-8L) // corrupt data
#define IFFERR_SYNTAX (-9L)  // IFF syntax error
#define IFFERR_NOTIFF (-10L) // not an IFF file
#define IFFERR_NOHOOK (-11L) // no stream hook installed

// ---- chunk IDs ------------------------------------------------------------
#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#define ID_FORM MAKE_ID('F', 'O', 'R', 'M')
#define ID_LIST MAKE_ID('L', 'I', 'S', 'T')
#define ID_PROP MAKE_ID('P', 'R', 'O', 'P')
#define ID_CAT MAKE_ID('C', 'A', 'T', ' ')
#define ID_NULL MAKE_ID(' ', ' ', ' ', ' ')

// IFFSIZE_UNKNOWN — pass to PushChunk for a chunk whose size is
// backpatched at PopChunk.
#define IFFSIZE_UNKNOWN (-1L)

// ---- ID helpers (amiga.lib-style header inlines, not LVOs) ----------------
// A byte is a valid IFF ID char if printable ASCII (0x20..0x7E).
static inline LONG GoodID(LONG id)
{
    for (int i = 0; i < 4; i++) {
        UBYTE c = (UBYTE)((ULONG)id >> (24 - 8 * i));
        if (c < 0x20 || c > 0x7E) {
            return 0;
        }
    }
    return 1;
}

// A valid type is a valid ID with no embedded spaces before a non-space
// and no leading space (the IFF type-ID rule); v0 uses the GoodID test
// plus a no-leading-space check.
static inline LONG GoodType(LONG type)
{
    if (!GoodID(type)) {
        return 0;
    }
    return ((UBYTE)((ULONG)type >> 24) != ' ') ? 1 : 0;
}

// Write the 4-char ID + NUL into buf (>=5 bytes); returns buf.
static inline UBYTE *IDtoStr(LONG id, UBYTE *buf)
{
    buf[0] = (UBYTE)((ULONG)id >> 24);
    buf[1] = (UBYTE)((ULONG)id >> 16);
    buf[2] = (UBYTE)((ULONG)id >> 8);
    buf[3] = (UBYTE)((ULONG)id);
    buf[4] = 0;
    return buf;
}

#endif // LIBRARIES_IFFPARSE_H
