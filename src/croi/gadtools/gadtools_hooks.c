// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks (LIB_OPEN/LIB_CLOSE/LIB_EXPUNGE/
// LIB_EXTFUNC) for gadtools.library — the `local`-flavour bodies the
// generated gadtools_vec.c points vec slots 0..3 at. Mirrors
// src/croi/dath/graphics_hooks.c.
//
// OpenLibrary/CloseLibrary("gadtools.library") are served by exec's
// SYS_OpenLibrary path; gadtools has no per-task setup, so these are
// trivial.

#include <cara/gadtools_lib.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <libraries/gadtools.h>

struct Library *Croi_GT_Open(struct Library *base, struct GadToolsBase *gtb)
{
    (void)gtb;
    return base;
}

void Croi_GT_Close(struct Library *base, struct GadToolsBase *gtb)
{
    (void)base;
    (void)gtb;
}

void Croi_GT_Expunge(struct Library *base, struct GadToolsBase *gtb)
{
    (void)base;
    (void)gtb; // gadtools.library is never expunged
}

ULONG Croi_GT_ExtFunc(struct Library *base, struct GadToolsBase *gtb)
{
    (void)base;
    (void)gtb;
    return 0; // V36+ contract: must return 0
}
