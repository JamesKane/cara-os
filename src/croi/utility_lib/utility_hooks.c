// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks (LIB_OPEN/LIB_CLOSE/LIB_EXPUNGE/
// LIB_EXTFUNC) for utility.library — the `local`-flavour bodies the
// generated utility_vec.c points vec slots 0..3 at. Mirrors
// src/croi/intuition_lib/intuition_hooks.c.
//
// Unlike the tag walkers in tag_ops.c, these run only library-
// internally: OpenLibrary/CloseLibrary("utility.library") are served by
// exec's SYS_OpenLibrary path (open_library.c) which bumps lib_OpenCnt
// directly, and may call lib_Open from S-mode — so these stay ordinary
// kernel text (NOT in the .lib_text.utility user RX page). utility.library
// has no per-task setup, so they are trivial. (Croi_LvoUnimplemented —
// the default body for every _PAD slot — is shared, in exec_hooks.c.)

#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <utility/utilitybase.h>

struct Library *Croi_Utility_Open(struct Library *base, struct UtilityBase *ub)
{
    (void)ub;
    return base;
}

void Croi_Utility_Close(struct Library *base, struct UtilityBase *ub)
{
    (void)base;
    (void)ub;
}

void Croi_Utility_Expunge(struct Library *base, struct UtilityBase *ub)
{
    (void)base;
    (void)ub; // utility.library is never expunged
}

ULONG Croi_Utility_ExtFunc(struct Library *base, struct UtilityBase *ub)
{
    (void)base;
    (void)ub;
    return 0; // V36+ contract: must return 0
}
