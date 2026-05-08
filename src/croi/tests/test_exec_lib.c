// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(exec_lib_smoke): exercises the Phase C exec.library
// substrate end-to-end from kernel-mode.
//
// The library was constructed at boot in croi_entry (see entry.c)
// via Croi_MakeLibrary, with the libBase placed at the linker-
// reserved 0x4000_0000 region (kernel-side direct-map view returned
// by Croi_ExecLib_KernelView). This test verifies:
//
//   1. The library is in the registry — Croi_OpenLibrary_Impl
//      returns the same base.
//   2. Version/Revision match what entry.c passed (V36.0).
//   3. lib_OpenCnt bumps on open, decrements on close.
//   4. The vec table at base[-1 - i] holds the trampoline pointers
//      (a sample read for OpenLibrary's slot is in the user-VA
//      range 0x4000_0000+).
//   5. AllocMem/FreeMem with MEMF_CLEAR returns zeroed memory.
//
// The user-mode side of this surface (an inline-stub call from a
// U-mode task that derefs SysBase) lands in Phase D once libcara
// can set the SysBase global before main.

#include <cara/exec_lib.h>
#include <cara/exec_lib_image.h>
#include <cara/test.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/types.h>

KERNEL_TEST(exec_lib_smoke)
{
    // ---- 1. registry lookup --------------------------------------------
    struct Library *base =
        Croi_OpenLibrary_Impl((STRPTR)(uintptr_t)"exec.library", 36);
    TEST_ASSERT(ctx, base != nullptr,
                "OpenLibrary('exec.library', 36) returned null");

    struct Library *base_kview = Croi_ExecLib_KernelView();
    TEST_ASSERT(ctx, base == base_kview,
                "OpenLibrary returned a different base than KernelView");

    // ---- 2. version/revision from entry.c -----------------------------
    TEST_ASSERT(ctx, base->lib_Version == 36, "lib_Version != 36");
    TEST_ASSERT(ctx, base->lib_Revision == 0, "lib_Revision != 0");

    // ---- 3. OpenCnt bumps via OpenLibrary, decrements via Close --------
    UWORD cnt_after_open = base->lib_OpenCnt;
    TEST_ASSERT(ctx, cnt_after_open >= 1,
                "lib_OpenCnt did not increment on OpenLibrary");

    Croi_CloseLibrary_Impl(base);
    TEST_ASSERT(ctx, base->lib_OpenCnt == cnt_after_open - 1,
                "lib_OpenCnt did not decrement on CloseLibrary");

    // ---- 4. vec[CARA_IDX_OpenLibrary] points into the user-VA region ---
    // OpenLibrary is the last user-LVO (ordinal 91). At base[-1 - 91]
    // we expect a pointer in 0x4000_xxxx (a trampoline in the
    // .lib_text.exec section of the .exec_lib region, mapped at
    // user VA 0x4000_0000+).
    void **vec = (void **)base;
    void  *open_lib_target = vec[-1 - 91];
    TEST_ASSERT(ctx,
                ((u64)(uptr)open_lib_target & ~0xFFFFull) ==
                (CARA_EXEC_LIB_USER_VA & ~0xFFFFull),
                "vec[OpenLibrary] does not point into 0x4000_0000+");

    // ---- 5. AllocMem with MEMF_CLEAR returns zeroed memory ------------
    APTR mem = Croi_AllocMem_Impl(64, MEMF_CLEAR);
    TEST_ASSERT(ctx, mem != nullptr,
                "AllocMem(64, MEMF_CLEAR) returned null");

    u8 *bytes = (u8 *)mem;
    for (int i = 0; i < 64; i++) {
        if (bytes[i] != 0) {
            Croi_FreeMem_Impl(mem, 64);
            TEST_FAIL(ctx, "AllocMem(MEMF_CLEAR) memory not zeroed");
        }
    }

    Croi_FreeMem_Impl(mem, 64);
}
