// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks for icon.library — the `local`-flavour
// bodies the generated icon_vec.c points vec slots 0..3 at. Mirrors
// src/croi/iffparse/iffparse_hooks.c.

#include <cara/icon_lib.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <workbench/icon.h>

struct Library *Croi_Icon_Open(struct Library *base, struct IconBase *ib)
{
    (void)ib;
    return base;
}

void Croi_Icon_Close(struct Library *base, struct IconBase *ib)
{
    (void)base;
    (void)ib;
}

void Croi_Icon_Expunge(struct Library *base, struct IconBase *ib)
{
    (void)base;
    (void)ib; // icon.library is never expunged
}

ULONG Croi_Icon_ExtFunc(struct Library *base, struct IconBase *ib)
{
    (void)base;
    (void)ib;
    return 0; // V36+ contract: must return 0
}
