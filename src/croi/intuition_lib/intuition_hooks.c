// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks (LIB_OPEN/LIB_CLOSE/LIB_EXPUNGE/
// LIB_EXTFUNC) for intuition.library — the `local`-flavour bodies the
// generated intuition_vec.c points vec slots 0..3 at. Mirrors
// src/croi/exec_lib/exec_hooks.c, but with the two-argument signature
// the .conf declares for intuition (V36+ base arg + the trailing
// struct IntuitionBase * lvo-gen appends).
//
// As with exec.library, OpenLibrary/CloseLibrary("intuition.library")
// are served by exec's SYS_OpenLibrary path (open_library.c), which
// bumps lib_OpenCnt directly. These hooks are the library-internal
// callbacks; intuition.library has no per-task setup in Phase 1, so
// they are trivial. (Croi_LvoUnimplemented — the default body for every
// _PAD slot — is shared with exec.library, defined in exec_hooks.c.)

#include <cara/intuition_lib.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <intuition/intuitionbase.h>

struct Library *Croi_Intuition_Open(struct Library *base, struct IntuitionBase *ib)
{
    (void)ib;
    return base;
}

void Croi_Intuition_Close(struct Library *base, struct IntuitionBase *ib)
{
    (void)base;
    (void)ib;
}

void Croi_Intuition_Expunge(struct Library *base, struct IntuitionBase *ib)
{
    (void)base;
    (void)ib; // intuition.library is never expunged in Phase 1
}

ULONG Croi_Intuition_ExtFunc(struct Library *base, struct IntuitionBase *ib)
{
    (void)base;
    (void)ib;
    return 0; // V36+ contract: must return 0
}
