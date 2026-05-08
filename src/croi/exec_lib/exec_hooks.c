// SPDX-License-Identifier: BSD-2-Clause
//
// Reserved-slot library hooks (LIB_OPEN/LIB_CLOSE/LIB_EXPUNGE/
// LIB_EXTFUNC) for exec.library, plus the shared default
// Croi_LvoUnimplemented body used by every `_PAD` row's vec entry.
//
// V36+ semantics:
//   lib_Open      — runs when OpenLibrary increments lib_OpenCnt to 1.
//                   Returns the library base (or nullptr to refuse).
//   lib_Close     — runs when CloseLibrary decrements lib_OpenCnt to 0.
//                   Returns segment list to expunge or 0 to keep
//                   resident.
//   lib_Expunge   — runs at expunge time (LIBF_DELEXP path), V0
//                   never expunges so this is unreachable.
//   lib_ExtFunc   — reserved-must-return-zero per RKM.
//
// For exec.library specifically, OpenLibrary/CloseLibrary in
// open_library.c already handle the OpenCnt update — these hooks are
// strictly the "library-internal" callbacks the kernel runs *after*
// the OpenCnt change. exec.library has no per-task setup work, so
// the hooks are trivial.

#include <cara/log.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/types.h>

struct Library *Croi_Exec_Open(struct Library *base)
{
    return base;
}

void Croi_Exec_Close(struct Library *base)
{
    (void)base;
}

void Croi_Exec_Expunge(struct Library *base)
{
    (void)base;     // exec.library is never expunged
}

ULONG Croi_Exec_ExtFunc(struct Library *base)
{
    (void)base;
    return 0;       // V36+ contract: must return 0
}

// Default body for every _PAD row. Returns 0 (covers void / int / ptr
// returns at the V36+ register-ABI level). Logs at most once per boot
// to surface accidental calls into a padding slot.
ULONG Croi_LvoUnimplemented(void)
{
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOG_WARN("lvoun", "call into a _PAD slot (returning 0)");
    }
    return 0;
}
