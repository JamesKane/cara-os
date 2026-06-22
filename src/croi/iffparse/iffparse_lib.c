// SPDX-License-Identifier: BSD-2-Clause
//
// iffparse.library handle lifecycle (L10.1, docs/IFFPARSE.md §2.1-2):
// AllocIFF/FreeIFF, InitIFFasDOS, OpenIFF/CloseIFF. The IFFHandle is a
// CaraIff on the SASOS shared heap whose public view (struct IFFHandle)
// sits at offset 0, so AllocIFF hands the caller &ci->pub and the impls
// recover CaraIff by cast. v0's only stream is a dos FileHandle bound via
// InitIFFasDOS + iff_Stream; the parse walk (ParseIFF, L10.2) and the
// write side (L10.3) read/write it via the dos impls.

#include <cara/alloc.h> // Croi_Free
#include <cara/iffparse_lib.h>
#include <cara/shared.h> // Croi_AllocShared
#include <cara/types.h>
#include <dos/dos.h>
#include <exec/types.h>
#include <libraries/iffparse.h>

struct IFFHandle *Croi_Iff_AllocIFF_Impl(void)
{
    struct CaraIff *ci = (struct CaraIff *)Croi_AllocShared(sizeof(struct CaraIff));
    if (!ci) {
        return nullptr;
    }
    *ci = (struct CaraIff){ 0 };
    return &ci->pub;
}

void Croi_Iff_FreeIFF_Impl(struct IFFHandle *iff)
{
    if (iff) {
        // iff == &ci->pub == the CaraIff base (pub at offset 0).
        Croi_Free(iff);
    }
}

// InitIFFasDOS: mark the handle as DOS-stream flavoured. The client sets
// iff_Stream to its open dos FileHandle before OpenIFF.
void Croi_Iff_InitIFFasDOS_Impl(struct IFFHandle *iff)
{
    if (!iff) {
        return;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    ci->as_dos = true;
}

// OpenIFF: bind the stream (from iff_Stream) + record the mode + reset the
// context stack. v0 reads/validates nothing here — ParseIFF (L10.2) reads
// the FORM header; an empty/garbage file surfaces as IFFERR_NOTIFF then.
LONG Croi_Iff_OpenIFF_Impl(struct IFFHandle *iff, LONG rwMode)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (!ci->as_dos) {
        return IFFERR_NOHOOK; // only InitIFFasDOS streams in v0
    }
    ci->stream = (BPTR)(uptr)iff->iff_Stream;
    if (!ci->stream) {
        return IFFERR_NOHOOK;
    }
    ci->mode = (ULONG)(rwMode & IFFF_RWBITS);
    ci->depth = 0;
    iff->iff_Depth = 0;
    iff->iff_Flags = (iff->iff_Flags & ~(ULONG)IFFF_RWBITS) | ci->mode;
    return 0;
}

// CloseIFF: drop the context stack. (The write-side flush lands in L10.3;
// the client owns + closes the dos FileHandle itself.)
void Croi_Iff_CloseIFF_Impl(struct IFFHandle *iff)
{
    if (!iff) {
        return;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    ci->depth = 0;
    ci->stream = (BPTR)0;
    iff->iff_Depth = 0;
}
