// SPDX-License-Identifier: BSD-2-Clause
//
// V36+ AllocMem / FreeMem — thin shims over the Phase 1 Croi heap.
//
// Flag handling on RV2 / Ky X1: there is no chip / fast / 24-bit-DMA
// distinction in CaraOS's SASOS heap, so MEMF_CHIP / MEMF_FAST /
// MEMF_24BITDMA / MEMF_KICK / MEMF_LOCAL are advisory. A first
// occurrence of each is logged at LOG_WARN with tag "memf"; subsequent
// occurrences are silent. MEMF_CLEAR (zero the allocation) and
// MEMF_REVERSE (allocate from heap tail) are honoured. MEMF_PUBLIC is
// the SASOS default and a no-op. MEMF_ANY = 0 means "any pool"; same
// as MEMF_PUBLIC under SASOS.

#include <cara/alloc.h>
#include <cara/exec_lib.h>
#include <cara/log.h>
#include <cara/types.h>
#include <exec/memory.h>

static void warn_advisory_once(ULONG flags)
{
    static bool warned_chip       = false;
    static bool warned_fast       = false;
    static bool warned_local      = false;
    static bool warned_24bit      = false;
    static bool warned_kick       = false;
    static bool warned_reverse    = false;

    if ((flags & MEMF_CHIP) && !warned_chip) {
        warned_chip = true;
        LOG_WARN("memf", "MEMF_CHIP is advisory on RV2 — pool ignored");
    }
    if ((flags & MEMF_FAST) && !warned_fast) {
        warned_fast = true;
        LOG_WARN("memf", "MEMF_FAST is advisory on RV2 — pool ignored");
    }
    if ((flags & MEMF_LOCAL) && !warned_local) {
        warned_local = true;
        LOG_WARN("memf", "MEMF_LOCAL is advisory on RV2 — pool ignored");
    }
    if ((flags & MEMF_24BITDMA) && !warned_24bit) {
        warned_24bit = true;
        LOG_WARN("memf", "MEMF_24BITDMA is advisory on RV2 — pool ignored");
    }
    if ((flags & MEMF_KICK) && !warned_kick) {
        warned_kick = true;
        LOG_WARN("memf", "MEMF_KICK is advisory on RV2 — pool ignored");
    }
    if ((flags & MEMF_REVERSE) && !warned_reverse) {
        warned_reverse = true;
        LOG_WARN("memf", "MEMF_REVERSE not yet honoured — head allocation");
    }
}

static void zero_bytes(void *p, ULONG n)
{
    u8 *b = (u8 *)p;
    for (ULONG i = 0; i < n; i++) {
        b[i] = 0;
    }
}

APTR Croi_AllocMem_Impl(ULONG size, ULONG flags)
{
    if (size == 0) {
        return nullptr;
    }
    warn_advisory_once(flags);

    void *p = Croi_Alloc((usize)size);
    if (!p) {
        return nullptr;
    }
    if (flags & MEMF_CLEAR) {
        zero_bytes(p, size);
    }
    return (APTR)p;
}

void Croi_FreeMem_Impl(APTR addr, ULONG size)
{
    // Croi's heap doesn't need the size hint — slabs and large
    // allocations recover the class id from the page header.
    (void)size;
    if (addr) {
        Croi_Free(addr);
    }
}
