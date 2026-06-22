// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ iffparse.library LVOs Croi
// serves (docs/IFFPARSE.md). iffparse is `syscall` flavour: each LVO
// reaches src/croi/syscall/syscall.c via its trampoline (Cara_Trampoline_
// Iff_* in src/croi/iffparse/trampolines.S), and the dispatcher routes
// the SYS_Iff_* to the Croi_Iff_*_Impl below. The impls parse kernel-side
// over a dos FileHandle (v0 InitIFFasDOS only — Croi_Dos_Read/Write/Seek);
// the IFFHandle + context stack live in the SASOS shared heap.

#ifndef CARA_IFFPARSE_LIB_H
#define CARA_IFFPARSE_LIB_H

#include <cara/types.h>
#include <dos/dos.h> // BPTR
#include <exec/types.h>
#include <libraries/iffparse.h>

struct Library;

#define CARA_IFF_MAXDEPTH 16
#define CARA_IFF_MAXSTOPS 8

// Kernel-private IFF handle. The public IFFHandle is at offset 0 (the
// AllocAslRequest pattern) so AllocIFF returns &ci->pub and the impls
// recover CaraIff by cast. The context stack + write-side backpatch
// positions are filled by ParseIFF (L10.2) / PushChunk (L10.3).
struct CaraIff {
    struct IFFHandle pub; // offset 0
    BPTR stream;          // dos FileHandle (bound at OpenIFF from iff_Stream)
    ULONG mode;           // IFFF_READ / IFFF_WRITE
    bool as_dos;          // InitIFFasDOS installed the DOS stream
    int depth;            // pushed context nodes (0 = none)
    struct ContextNode nodes[CARA_IFF_MAXDEPTH];
    LONG push_pos[CARA_IFF_MAXDEPTH]; // stream offset of each pushed chunk's size field
    // ParseIFF SCAN stop registry (L10.2).
    struct {
        LONG type, id;
    } stops[CARA_IFF_MAXSTOPS];
    int nstops;
    struct {
        LONG type, id;
    } exit_stops[CARA_IFF_MAXSTOPS];
    int nexit;
};

// ---- Handle lifecycle (L10.1) ---------------------------------------
struct IFFHandle *Croi_Iff_AllocIFF_Impl(void);
void Croi_Iff_FreeIFF_Impl(struct IFFHandle *iff);
void Croi_Iff_InitIFFasDOS_Impl(struct IFFHandle *iff);
LONG Croi_Iff_OpenIFF_Impl(struct IFFHandle *iff, LONG rwMode);
void Croi_Iff_CloseIFF_Impl(struct IFFHandle *iff);

// ---- The read walk (L10.2) ------------------------------------------
LONG Croi_Iff_ParseIFF_Impl(struct IFFHandle *iff, LONG control);
LONG Croi_Iff_StopChunk_Impl(struct IFFHandle *iff, LONG type, LONG id);
LONG Croi_Iff_StopOnExit_Impl(struct IFFHandle *iff, LONG type, LONG id);
LONG Croi_Iff_ReadChunkBytes_Impl(struct IFFHandle *iff, APTR buf, LONG size);
LONG Croi_Iff_ReadChunkRecords_Impl(struct IFFHandle *iff, APTR buf, LONG recSize, LONG numRec);
struct ContextNode *Croi_Iff_CurrentChunk_Impl(struct IFFHandle *iff);
struct ContextNode *Croi_Iff_ParentChunk_Impl(struct ContextNode *cn);

// ---- Reserved-slot library hooks (`local` flavour) ------------------
struct IFFParseBase;
struct Library *Croi_Iff_Open(struct Library *base, struct IFFParseBase *ib);
void Croi_Iff_Close(struct Library *base, struct IFFParseBase *ib);
void Croi_Iff_Expunge(struct Library *base, struct IFFParseBase *ib);
ULONG Croi_Iff_ExtFunc(struct Library *base, struct IFFParseBase *ib);

#endif // CARA_IFFPARSE_LIB_H
