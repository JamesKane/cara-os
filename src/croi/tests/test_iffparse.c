// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(iffparse_lifecycle): L10.1 — iffparse.library handle
// lifecycle (docs/IFFPARSE.md). iffparse is `syscall` flavour, so the
// impls are callable from the S-mode runner. Opens a dos file as the IFF
// stream, allocs an IFFHandle, InitIFFasDOS + binds the stream, OpenIFF /
// CloseIFF round-trips, FreeIFF. The parse/write surface arrives in L10.2+.

#include <cara/iffparse_lib.h>
#include <cara/test.h>
#include <cara/types.h>
#include <dos/dos.h>
#include <libraries/iffparse.h>

KERNEL_TEST(iffparse_lifecycle)
{
    // ID helpers (header inlines).
    TEST_ASSERT(ctx, GoodID(ID_FORM) && GoodType(ID_FORM), "FORM is a good id/type");
    TEST_ASSERT(ctx, !GoodType(ID_NULL), "all-spaces is not a good type");
    UBYTE idbuf[5];
    IDtoStr(ID_FORM, idbuf);
    TEST_ASSERT(ctx,
                idbuf[0] == 'F' && idbuf[1] == 'O' && idbuf[2] == 'R' && idbuf[3] == 'M' &&
                    idbuf[4] == 0,
                "IDtoStr(FORM)");

    struct IFFHandle *iff = Croi_Iff_AllocIFF_Impl();
    TEST_ASSERT(ctx, iff != nullptr, "AllocIFF");

    // OpenIFF before InitIFFasDOS → no stream hook.
    TEST_ASSERT(ctx, Croi_Iff_OpenIFF_Impl(iff, IFFF_WRITE) == IFFERR_NOHOOK,
                "OpenIFF without a stream hook fails");

    // L10.1 does no I/O — OpenIFF only binds the stream + mode. Use a
    // fake (never-dereferenced) FileHandle; a real CaraFS file needs a
    // Process context, so the parse/write round-trip is a Gleas test in
    // L10.2+.
    Croi_Iff_InitIFFasDOS_Impl(iff);
    BPTR fake = (BPTR)(uptr)0xF00D;
    iff->iff_Stream = (IPTR)(uptr)fake; // the client binds its FileHandle
    TEST_ASSERT(ctx, Croi_Iff_OpenIFF_Impl(iff, IFFF_WRITE) == 0, "OpenIFF (write)");
    TEST_ASSERT(ctx, (iff->iff_Flags & IFFF_RWBITS) == IFFF_WRITE, "write flag recorded");

    // The kernel-private view: stream + mode bound, context stack empty.
    struct CaraIff *ci = (struct CaraIff *)iff; // pub at offset 0
    TEST_ASSERT(ctx, ci->stream == fake && ci->as_dos && ci->depth == 0, "stream bound, depth 0");

    Croi_Iff_CloseIFF_Impl(iff);
    TEST_ASSERT(ctx, iff->iff_Depth == 0, "CloseIFF reset depth");
    Croi_Iff_FreeIFF_Impl(iff);

    Croi_Iff_FreeIFF_Impl(nullptr); // null free is safe
}
