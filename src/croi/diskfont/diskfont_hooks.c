// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks for diskfont.library — the `local`-flavour
// bodies the generated diskfont_vec.c points vec slots 0..3 at. Mirrors
// src/croi/icon/icon_hooks.c.

#include <cara/diskfont_lib.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <libraries/diskfont.h>

struct Library *Croi_Diskfont_Open(struct Library *base, struct DiskfontBase *db)
{
    (void)db;
    return base;
}

void Croi_Diskfont_Close(struct Library *base, struct DiskfontBase *db)
{
    (void)base;
    (void)db;
}

void Croi_Diskfont_Expunge(struct Library *base, struct DiskfontBase *db)
{
    (void)base;
    (void)db; // diskfont.library is never expunged
}

ULONG Croi_Diskfont_ExtFunc(struct Library *base, struct DiskfontBase *db)
{
    (void)base;
    (void)db;
    return 0; // V36+ contract: must return 0
}
