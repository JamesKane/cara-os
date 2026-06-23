// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks for expansion.library — the `local`-flavour
// bodies the generated expansion_vec.c points vec slots 0..3 at. Mirrors
// src/croi/icon/icon_hooks.c.

#include <cara/expansion_lib.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <libraries/configvars.h>

struct Library *Croi_Exp_Open(struct Library *base, struct ExpansionBase *eb)
{
    (void)eb;
    return base;
}

void Croi_Exp_Close(struct Library *base, struct ExpansionBase *eb)
{
    (void)base;
    (void)eb;
}

void Croi_Exp_Expunge(struct Library *base, struct ExpansionBase *eb)
{
    (void)base;
    (void)eb; // expansion.library is never expunged
}

ULONG Croi_Exp_ExtFunc(struct Library *base, struct ExpansionBase *eb)
{
    (void)base;
    (void)eb;
    return 0; // V36+ contract: must return 0
}
