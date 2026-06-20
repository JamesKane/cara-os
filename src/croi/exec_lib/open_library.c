// SPDX-License-Identifier: BSD-2-Clause
//
// V36+ OpenLibrary / OldOpenLibrary / CloseLibrary impls.
//
// OpenLibrary("name", version) looks up the library by name in the
// kernel registry, asserts that lib_Version >= requested version, and
// atomic-bumps lib_OpenCnt. Returns nullptr if the library isn't
// resident or if its version is too old.
//
// OldOpenLibrary just reduces to OpenLibrary("name", 0) — accepts any
// version. Both impls share the same function via a thin forwarder.
//
// CloseLibrary atomic-decrements lib_OpenCnt; v0 never expunges
// (LIBF_DELEXP handling is post-v0).
//
// Both Open functions ought to chain into the library's own lib_Open
// hook (vec[CARA_IDX_LIB_OPEN]) per V36+ semantics — that's what gives a
// library a chance to do per-task setup. v0 skips that for
// exec.library specifically (its Open hook is a no-op anyway), and
// will revisit when graphics.library lands in Phase 4.

#include <cara/exec_lib.h>
#include <cara/log.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>

#include "library_registry.h"

#include <stdatomic.h>

struct Library *Croi_OpenLibrary_Impl(STRPTR name, ULONG version)
{
    if (!name) {
        return nullptr;
    }
    struct Library *lib = Croi_FindLibraryByName(name);
    if (!lib) {
        LOG_INFO("oplib", "OpenLibrary: '%s' not resident", name);
        return nullptr;
    }
    if (version != 0 && lib->lib_Version < version) {
        LOG_INFO("oplib", "OpenLibrary: '%s' V%u < requested V%u", name, lib->lib_Version, version);
        return nullptr;
    }
    // lib_OpenCnt is UWORD; treat the field as if atomic for the
    // single-hart case (and revisit when SMP arrives).
    lib->lib_OpenCnt++;
    return lib;
}

struct Library *Croi_OldOpenLibrary_Impl(STRPTR name)
{
    return Croi_OpenLibrary_Impl(name, 0);
}

void Croi_CloseLibrary_Impl(struct Library *lib)
{
    if (!lib) {
        return;
    }
    if (lib->lib_OpenCnt == 0) {
        LOG_WARN("oplib", "CloseLibrary on '%s' with OpenCnt=0",
                 lib->lib_Node.ln_Name ? lib->lib_Node.ln_Name : "(unnamed)");
        return;
    }
    lib->lib_OpenCnt--;
    // v0: never expunge.
}
