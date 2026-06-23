// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks for commodities.library — the `local`-flavour
// bodies the generated commodities_vec.c points vec slots 0..3 at. Mirrors
// src/croi/icon/icon_hooks.c.

#include <cara/commodities_lib.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <libraries/commodities.h>

struct Library *Croi_Cx_Open(struct Library *base, struct CxBase *cb)
{
    (void)cb;
    return base;
}

void Croi_Cx_Close(struct Library *base, struct CxBase *cb)
{
    (void)base;
    (void)cb;
}

void Croi_Cx_Expunge(struct Library *base, struct CxBase *cb)
{
    (void)base;
    (void)cb; // commodities.library is never expunged
}

ULONG Croi_Cx_ExtFunc(struct Library *base, struct CxBase *cb)
{
    (void)base;
    (void)cb;
    return 0; // V36+ contract: must return 0
}
