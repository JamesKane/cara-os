// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks (LIB_OPEN/LIB_CLOSE/LIB_EXPUNGE/
// LIB_EXTFUNC) for asl.library — the `local`-flavour bodies the
// generated asl_vec.c points vec slots 0..3 at. Mirrors
// src/croi/gadtools/gadtools_hooks.c.

#include <cara/asl_lib.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <libraries/asl.h>

struct Library *Croi_Asl_Open(struct Library *base, struct AslBase *ab)
{
    (void)ab;
    return base;
}

void Croi_Asl_Close(struct Library *base, struct AslBase *ab)
{
    (void)base;
    (void)ab;
}

void Croi_Asl_Expunge(struct Library *base, struct AslBase *ab)
{
    (void)base;
    (void)ab; // asl.library is never expunged
}

ULONG Croi_Asl_ExtFunc(struct Library *base, struct AslBase *ab)
{
    (void)base;
    (void)ab;
    return 0; // V36+ contract: must return 0
}
