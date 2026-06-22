// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks for iffparse.library — the `local`-flavour
// bodies the generated iffparse_vec.c points vec slots 0..3 at. Mirrors
// src/croi/asl/asl_hooks.c.

#include <cara/iffparse_lib.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <libraries/iffparse.h>

struct Library *Croi_Iff_Open(struct Library *base, struct IFFParseBase *ib)
{
    (void)ib;
    return base;
}

void Croi_Iff_Close(struct Library *base, struct IFFParseBase *ib)
{
    (void)base;
    (void)ib;
}

void Croi_Iff_Expunge(struct Library *base, struct IFFParseBase *ib)
{
    (void)base;
    (void)ib; // iffparse.library is never expunged
}

ULONG Croi_Iff_ExtFunc(struct Library *base, struct IFFParseBase *ib)
{
    (void)base;
    (void)ib;
    return 0; // V36+ contract: must return 0
}
