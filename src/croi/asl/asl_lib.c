// SPDX-License-Identifier: BSD-2-Clause
//
// asl.library requester lifecycle (L9.1, docs/LEARGAS_ASL.md §2.3):
// AllocAslRequest / FreeAslRequest (+ the legacy FreeFileRequest). The
// requester is a CaraAslReq on the SASOS shared heap whose public view
// (FileRequester / FontRequester / ScreenModeRequester) sits at offset 0,
// so the caller's opaque APTR is &req->pub and we recover the CaraAslReq
// by casting it back. Config tags are parsed into the kernel-private tail
// + the owned path buffers; AslRequest (L9.2) runs the modal loop.

#include <cara/alloc.h> // Croi_Free
#include <cara/asl_lib.h>
#include <cara/shared.h>  // Croi_AllocShared
#include <cara/tagitem.h> // Croi_GetTagData
#include <cara/types.h>
#include <exec/types.h>
#include <libraries/asl.h>

// Copy at most max-1 bytes of src into dst, NUL-terminating (src may be
// null → empty).
static void asl_strcopy(char *dst, const char *src, int max)
{
    int n = 0;
    if (src) {
        while (src[n] && n < max - 1) {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = '\0';
}

APTR Croi_Asl_AllocAslRequest_Impl(ULONG reqType, struct TagItem *tags)
{
    struct CaraAslReq *req = (struct CaraAslReq *)Croi_AllocShared(sizeof(struct CaraAslReq));
    if (!req) {
        return nullptr;
    }
    *req = (struct CaraAslReq){ 0 };
    req->type = reqType;

    switch (reqType) {
    case ASL_FileRequest:
        // Owned dir/file buffers, seeded from the initial-path tags.
        asl_strcopy(req->dirbuf, (const char *)(uptr)Croi_GetTagData(tags, ASLFR_InitialDrawer, 0),
                    CARA_ASL_DIRMAX);
        asl_strcopy(req->filebuf, (const char *)(uptr)Croi_GetTagData(tags, ASLFR_InitialFile, 0),
                    CARA_ASL_FILEMAX);
        req->pub.file.rf_Dir = req->dirbuf;
        req->pub.file.rf_File = req->filebuf;
        req->title = (const char *)(uptr)Croi_GetTagData(tags, ASLFR_TitleText, 0);
        req->parent = (struct Window *)(uptr)Croi_GetTagData(tags, ASLFR_Window, 0);
        break;
    case ASL_FontRequest:
        req->title = (const char *)(uptr)Croi_GetTagData(tags, ASLFO_TitleText, 0);
        req->parent = (struct Window *)(uptr)Croi_GetTagData(tags, ASLFO_Window, 0);
        break;
    case ASL_ScreenModeRequest:
        req->title = (const char *)(uptr)Croi_GetTagData(tags, ASLSM_TitleText, 0);
        req->parent = (struct Window *)(uptr)Croi_GetTagData(tags, ASLSM_Window, 0);
        break;
    default:
        // Unknown type — still a valid (empty) requester; AslRequest
        // will reject it.
        break;
    }
    return &req->pub;
}

void Croi_Asl_FreeAslRequest_Impl(APTR requester)
{
    if (requester) {
        // requester == &req->pub == the CaraAslReq base (pub at offset 0).
        Croi_Free(requester);
    }
}

// Legacy V36 alias.
void Croi_Asl_FreeFileRequest_Impl(struct FileRequester *fileReq)
{
    Croi_Asl_FreeAslRequest_Impl(fileReq);
}
