// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ asl.library LVOs Croi serves
// (docs/LEARGAS_ASL.md). asl is `syscall` flavour: each LVO reaches
// src/croi/syscall/syscall.c via its trampoline (Cara_Trampoline_Asl_* in
// src/croi/asl/trampolines.S), and the dispatcher routes the SYS_Asl_* to
// the Croi_Asl_*_Impl below. The bodies compose the L5 modal requester
// core + the L8 gadtools kinds + dos directory listing — asl adds no new
// substrate (docs/LEARGAS_ASL.md §2.1).

#ifndef CARA_ASL_LIB_H
#define CARA_ASL_LIB_H

#include <cara/types.h>
#include <exec/types.h>
#include <libraries/asl.h>

struct TagItem;
struct Library;
struct Window;

// Path/name buffer sizes for the v0 string-entry file requester.
#define CARA_ASL_DIRMAX 256
#define CARA_ASL_FILEMAX 108

// Kernel-private requester. The public requester struct is the FIRST
// member (offset 0) so AllocAslRequest can hand the caller &req->pub as
// the opaque APTR and FreeAslRequest / AslRequest recover the CaraAslReq
// by casting that pointer back. `type` (after the union) selects which
// public view is live; the config + owned path buffers follow.
struct CaraAslReq {
    union {
        struct FileRequester file;
        struct FontRequester font;
        struct ScreenModeRequester sm;
    } pub;
    u32 type; // ASL_*
    const char *title;
    struct Window *parent;
    char dirbuf[CARA_ASL_DIRMAX];
    char filebuf[CARA_ASL_FILEMAX];
};

// ---- Requester lifecycle (L9.1) -------------------------------------
APTR Croi_Asl_AllocAslRequest_Impl(ULONG reqType, struct TagItem *tags);
void Croi_Asl_FreeAslRequest_Impl(APTR requester);
void Croi_Asl_FreeFileRequest_Impl(struct FileRequester *fileReq); // legacy alias

// ---- Reserved-slot library hooks (`local` flavour) ------------------
struct AslBase;
struct Library *Croi_Asl_Open(struct Library *base, struct AslBase *ab);
void Croi_Asl_Close(struct Library *base, struct AslBase *ab);
void Croi_Asl_Expunge(struct Library *base, struct AslBase *ab);
ULONG Croi_Asl_ExtFunc(struct Library *base, struct AslBase *ab);

#endif // CARA_ASL_LIB_H
