// SPDX-License-Identifier: BSD-2-Clause
//
// Croi_MakeLibrary — kernel-internal library construction. Walks a
// MakeLibrary TagItem array (cara/makelibrary.h) and populates a
// `struct Library` such that the negative-side function-pointer table
// is in place at base - 8 * (i + 1) for runtime ordinal i.
//
// Two construction modes:
//
//   - MKL_BASE absent (the default): heap-allocate
//     `lib_NegSize + lib_PosSize` bytes; the libBase is the byte at
//     offset lib_NegSize within the allocation. The vec table is
//     written into the bytes preceding libBase (in declaration order
//     vs. decreasing memory address — see docs/LVO.md §3 / §7).
//
//   - MKL_BASE present: the caller has reserved a contiguous
//     [base - lib_NegSize, base + lib_PosSize) region (typically by
//     a linker script for libraries living in the
//     0x0000_0000_4000_0000 shared region). Croi_MakeLibrary just
//     populates the `struct Library` and writes the vec entries
//     into the linker-reserved negative side.
//
// On success returns the libBase pointer; on failure returns
// nullptr after a LOG_FATAL with a reason.

#include <cara/alloc.h>
#include <cara/exec_lib.h>
#include <cara/log.h>
#include <cara/makelibrary.h>
#include <cara/tagitem.h>
#include <cara/types.h>
#include <exec/libraries.h>
#include <exec/nodes.h>
#include <exec/types.h>
#include <utility/tagitem.h>

#include "library_registry.h"

struct Library *Croi_MakeLibrary(const struct TagItem *tags)
{
    const char *name =
        (const char *)Croi_GetTagData(tags, MKL_NAME, 0);
    UWORD version    = (UWORD)Croi_GetTagData(tags, MKL_VERSION, 0);
    UWORD revision   = (UWORD)Croi_GetTagData(tags, MKL_REVISION, 0);
    void **vec       = (void **)Croi_GetTagData(tags, MKL_VEC_TABLE, 0);
    ULONG  vec_count = (ULONG)Croi_GetTagData(tags, MKL_VEC_COUNT, 0);
    ULONG  priv_size = (ULONG)Croi_GetTagData(tags, MKL_PRIVATE_SIZE, 0);
    void (*init_fn)(struct Library *) =
        (void (*)(struct Library *))Croi_GetTagData(tags, MKL_INIT_FN, 0);
    struct Library *base =
        (struct Library *)Croi_GetTagData(tags, MKL_BASE, 0);

    if (!name) {
        LOG_FATAL("mklib", "Croi_MakeLibrary: missing MKL_NAME");
        return nullptr;
    }
    if (!vec) {
        LOG_FATAL("mklib", "%s: missing MKL_VEC_TABLE", name);
        return nullptr;
    }
    if (vec_count == 0) {
        LOG_FATAL("mklib", "%s: MKL_VEC_COUNT is 0", name);
        return nullptr;
    }

    UWORD neg_size = (UWORD)(sizeof(void *) * vec_count);
    UWORD pos_size = (UWORD)(sizeof(struct Library) + priv_size);

    if (!base) {
        // Heap-allocate the library image. Layout:
        //     [neg_size bytes (vec table)] [pos_size bytes (struct Library + private)]
        // libBase = allocation_start + neg_size.
        u8 *block = (u8 *)Croi_Alloc((usize)neg_size + (usize)pos_size);
        if (!block) {
            LOG_FATAL("mklib", "%s: heap alloc failed (%u + %u bytes)",
                      name, (unsigned)neg_size, (unsigned)pos_size);
            return nullptr;
        }
        base = (struct Library *)(block + neg_size);
    }

    // Populate the negative-side function-pointer table.
    // ((void **)base)[-1 - i] receives vec[i] — see docs/LVO.md §3.
    void **vec_dst = (void **)base;
    for (ULONG i = 0; i < vec_count; i++) {
        vec_dst[-1 - (long)i] = vec[i];
    }

    // V36+ struct Library fields.
    // V36+ ln_Name / lib_IdString are non-const `char *` / `APTR` per
    // the public ABI. The cast through (uintptr_t) makes the
    // const-strip from `const char *name` explicit; the storage is
    // owned by the caller (typically a string literal in .rodata)
    // and the kernel never writes through it.
    base->lib_Node.ln_Type = NT_LIBRARY;
    base->lib_Node.ln_Pri  = 0;
    base->lib_Node.ln_Name = (char *)(uintptr_t)name;
    base->lib_Node.ln_Succ = nullptr;          // set by Croi_RegisterLibrary
    base->lib_Node.ln_Pred = nullptr;
    base->lib_Flags        = 0;
    base->lib_pad          = 0;
    base->lib_NegSize      = neg_size;
    base->lib_PosSize      = pos_size;
    base->lib_Version      = version;
    base->lib_Revision     = revision;
    base->lib_IdString     = (APTR)(uintptr_t)name;
    base->lib_Sum          = 0;
    base->lib_OpenCnt      = 0;

    // Zero the private state past the public prefix so the library's
    // own init_fn sees a clean slate.
    if (priv_size > 0) {
        u8 *priv = (u8 *)base + sizeof(struct Library);
        for (ULONG i = 0; i < priv_size; i++) {
            priv[i] = 0;
        }
    }

    Croi_RegisterLibrary(base);

    if (init_fn) {
        init_fn(base);
    }

    LOG_INFO("mklib", "registered '%s' V%u.%u (neg=%u, pos=%u, base=0x%llx)",
             name, (unsigned)version, (unsigned)revision,
             (unsigned)neg_size, (unsigned)pos_size, (u64)(uptr)base);
    return base;
}
