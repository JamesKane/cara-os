// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks (LIB_OPEN/LIB_CLOSE/LIB_EXPUNGE/
// LIB_EXTFUNC) for graphics.library — the `local`-flavour bodies the
// generated graphics_vec.c points vec slots 0..3 at. Mirrors
// src/logaic/dos/dos_hooks.c.
//
// OpenLibrary/CloseLibrary("graphics.library") are served by exec's
// SYS_OpenLibrary path (open_library.c). graphics.library has no
// per-task setup in L4.1, so these are trivial. (Croi_LvoUnimplemented —
// the default body for every _PAD slot — is shared, in exec_hooks.c.)

#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <graphics/gfxbase.h>

struct Library *Croi_Gfx_Open(struct Library *base, struct GfxBase *gb)
{
    (void)gb;
    return base;
}

void Croi_Gfx_Close(struct Library *base, struct GfxBase *gb)
{
    (void)base;
    (void)gb;
}

void Croi_Gfx_Expunge(struct Library *base, struct GfxBase *gb)
{
    (void)base;
    (void)gb; // graphics.library is never expunged
}

ULONG Croi_Gfx_ExtFunc(struct Library *base, struct GfxBase *gb)
{
    (void)base;
    (void)gb;
    return 0; // V36+ contract: must return 0
}
