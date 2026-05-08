// SPDX-License-Identifier: BSD-2-Clause
//
// Internal API for the kernel-side library name registry. Used by
// Croi_MakeLibrary (registers) and Croi_OpenLibrary_Impl (looks up).

#ifndef CROI_EXEC_LIB_LIBRARY_REGISTRY_H
#define CROI_EXEC_LIB_LIBRARY_REGISTRY_H

#include <cara/types.h>

struct Library;

// Append a constructed library's lib_Node to the registry. The
// lib->lib_Node.ln_Type / ln_Name / ln_Pri must be set already
// (Croi_MakeLibrary does this).
void Croi_RegisterLibrary(struct Library *lib);

// O(N) lookup; N is the number of resident libraries.
struct Library *Croi_FindLibraryByName(const char *name);

usize Croi_LibList_Count(void);

#endif
