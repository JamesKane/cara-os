// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks (LIB_OPEN/LIB_CLOSE/LIB_EXPUNGE/
// LIB_EXTFUNC) for dos.library — the `local`-flavour bodies the
// generated dos_vec.c points vec slots 0..3 at. Mirrors
// src/croi/intuition_lib/intuition_hooks.c.
//
// OpenLibrary/CloseLibrary("dos.library") are served by exec's
// SYS_OpenLibrary path (open_library.c). dos.library has no per-task
// setup in L3.1, so these are trivial. (Croi_LvoUnimplemented — the
// default body for every _PAD slot — is shared, in exec_hooks.c.)

#include <cara/types.h>
#include <dos/dosextens.h>
#include <exec/libraries.h>
#include <exec/types.h>

struct Library *Croi_Dos_Open(struct Library *base, struct DosLibrary *db)
{
    (void)db;
    return base;
}

void Croi_Dos_Close(struct Library *base, struct DosLibrary *db)
{
    (void)base;
    (void)db;
}

void Croi_Dos_Expunge(struct Library *base, struct DosLibrary *db)
{
    (void)base;
    (void)db; // dos.library is never expunged
}

ULONG Croi_Dos_ExtFunc(struct Library *base, struct DosLibrary *db)
{
    (void)base;
    (void)db;
    return 0; // V36+ contract: must return 0
}
