// SPDX-License-Identifier: BSD-2-Clause
//
// MakeLibrary tag identifiers — the parameter list shape for
// Croi_MakeLibrary, fixed by docs/LVO.md §12.1. Allocated in the
// utility.library TAG_USER range so they coexist cleanly with any
// user tag IDs a Phase 3+ caller might add.
//
// tools/lvo-gen emits a per-library `<libname>_make_args[]` populated
// with these tags pointing at the generated vec table; the library's
// hand-written init code patches in MKL_PRIVATE_SIZE / MKL_VERSION /
// MKL_REVISION / MKL_INIT_FN / MKL_SERVER_PORT_KOBJ before passing
// the array to Croi_MakeLibrary.

#ifndef CARA_MAKELIBRARY_H
#define CARA_MAKELIBRARY_H

#include <utility/tagitem.h>

// Fixed-width enum so values past INT_MAX (TAG_USER = 0x80000000) fit
// without -Wpedantic griping; Tag is the canonical V36+ ULONG type.
enum : Tag {
    MKL_NAME             = TAG_USER + 1,    // STRPTR  — library filename
    MKL_VERSION          = TAG_USER + 2,    // UWORD   — lib_Version
    MKL_REVISION         = TAG_USER + 3,    // UWORD   — lib_Revision
    MKL_VEC_TABLE        = TAG_USER + 4,    // void ** — generated vec source array
    MKL_VEC_COUNT        = TAG_USER + 5,    // ULONG   — element count of vec
    MKL_PRIVATE_SIZE     = TAG_USER + 6,    // ULONG   — bytes past struct Library
    MKL_INIT_FN          = TAG_USER + 7,    // void(*)(struct Library *)
    MKL_SERVER_PORT_KOBJ = TAG_USER + 8,    // Handle  — receiving Gleas's port; 0 if none

    // Optional: a pre-allocated base address (linker-placed library).
    // When absent, Croi_MakeLibrary heap-allocates lib_NegSize +
    // lib_PosSize bytes and treats `<allocation> + lib_NegSize` as
    // the libBase. When present, the caller has reserved a
    // contiguous lib_NegSize + lib_PosSize region with the
    // libBase pointer at the boundary, and Croi_MakeLibrary just
    // populates the fields and copies the vec table down.
    MKL_BASE             = TAG_USER + 9,    // struct Library *  (optional)
};

#endif
