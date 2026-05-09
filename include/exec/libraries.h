// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ library base structure. Every library on disk and in
// memory begins with `struct Library` at the library base; the
// negative side of the base holds the function-pointer table (LVO
// table, see docs/LVO.md). User programs treat the returned
// `struct Library *` from OpenLibrary as the library base — they read
// lib_Version / lib_Revision directly, they never index into the
// negative side themselves (the public proto/<lib>.h inline stubs do
// that for them).
//
// On RV64 the runtime negative-side stride is sizeof(void *) = 8
// rather than the 68k LIB_VECTSIZE = 6, but the LIB_VECTSIZE constant
// is preserved as a header value for V36+ source-level compatibility
// — see docs/LVO.md §3.1 for the deviation note. Programs that
// programmatically index the LVO table by negative-byte arithmetic
// break; programs that use the canonical proto/<lib>.h stubs do not.

#ifndef EXEC_LIBRARIES_H
#define EXEC_LIBRARIES_H

#include <exec/nodes.h>
#include <exec/types.h>

struct Library {
    struct Node lib_Node;
    UBYTE lib_Flags;
    UBYTE lib_pad;
    UWORD lib_NegSize; // bytes used by negative-side LVO table
    UWORD lib_PosSize; // bytes used by positive side (struct + private)
    UWORD lib_Version;
    UWORD lib_Revision;
    APTR lib_IdString;
    ULONG lib_Sum; // checksum (when LIBF_SUMUSED)
    UWORD lib_OpenCnt;
};

// V36+ lib_Flags bits.
#define LIBF_SUMMING (1 << 0) // checksum currently in progress
#define LIBF_CHANGED (1 << 1) // checksum invalidated
#define LIBF_SUMUSED (1 << 2) // library uses checksumming
#define LIBF_DELEXP (1 << 3)  // delayed expunge requested

// V36+ canonical reserved-LVO offsets and stride. These are header
// constants and Phase 9 lookup keys, NOT physical memory offsets at
// runtime on RV64 (docs/LVO.md §3.1).
#define LIB_OPEN (-6)
#define LIB_CLOSE (-12)
#define LIB_EXPUNGE (-18)
#define LIB_EXTFUNC (-24)      // reserved; must return zero
#define LIB_USERDEF (-30)      // first user-defined LVO
#define LIB_NONSTD LIB_USERDEF // V36+ alternate name
#define LIB_VECTSIZE 6         // 68k stride; runtime stride on RV64 is 8

#endif // EXEC_LIBRARIES_H
