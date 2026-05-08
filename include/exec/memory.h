// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ exec memory flags and structures.
//
// On the original Amiga, MEMF_CHIP / MEMF_FAST / MEMF_24BITDMA / etc.
// distinguished different physical memory pools (chip RAM accessible
// to custom-chip DMA, fast RAM not on the bus, etc.). On RV2 / Ky X1
// every visible page of DRAM is identical in capability — there are
// no separate chip / fast pools. CaraOS preserves the flag values
// verbatim for V36+ source-level compatibility; the kernel
// implementation in src/croi/exec_lib/mem.c (Phase C) treats the
// pool-selection bits as advisory and serves any allocation from the
// shared system heap (ARCHITECTURE.md §4.3 — the 0x1_0000_0000 region).
//
// MEMF_CLEAR is honoured (zeroes the allocation).
// MEMF_REVERSE is honoured (allocates from the tail of the heap).
// MEMF_PUBLIC is the SASOS default and a no-op on CaraOS.
// MEMF_LOCAL / MEMF_KICK / MEMF_24BITDMA / MEMF_CHIP are advisory and
//   logged the first time each is seen with the LOG_WARN tag "memf".

#ifndef EXEC_MEMORY_H
#define EXEC_MEMORY_H

#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/types.h>

// V36+ exec/memory.i flag bits (verbatim values).
#define MEMF_ANY        0UL
#define MEMF_PUBLIC     (1UL <<  0)   // 0x00000001
#define MEMF_CHIP       (1UL <<  1)   // 0x00000002
#define MEMF_FAST       (1UL <<  2)   // 0x00000004
#define MEMF_LOCAL      (1UL <<  8)   // 0x00000100
#define MEMF_24BITDMA   (1UL <<  9)   // 0x00000200
#define MEMF_KICK       (1UL << 10)   // 0x00000400
#define MEMF_CLEAR      (1UL << 16)   // 0x00010000
#define MEMF_LARGEST    (1UL << 17)   // 0x00020000  (AvailMem mode)
#define MEMF_REVERSE    (1UL << 18)   // 0x00040000
#define MEMF_TOTAL      (1UL << 19)   // 0x00080000  (AvailMem mode)
#define MEMF_NO_EXPUNGE (1UL << 31)   // 0x80000000  (V39+ — accepted)

// MemEntry / MemList — the per-task memory-tracking shape used by
// tc_MemEntry. Tasks created via CreateTask hand a MemList describing
// every block to be freed when the task exits. On CaraOS the shape is
// preserved so V36+ source compiles; the kernel-internal lifecycle
// (which actually frees on exit) lives in the brand namespace.
struct MemEntry {
    union {
        ULONG meu_Reqs;     // requirements (MEMF_*)
        APTR  meu_Addr;     // address (after allocation)
    } me_Un;
    ULONG  me_Length;
};
#define me_Reqs me_Un.meu_Reqs
#define me_Addr me_Un.meu_Addr

struct MemList {
    struct Node      ml_Node;
    UWORD            ml_NumEntries;
    struct MemEntry  ml_ME[1];      // variable length
};

#endif // EXEC_MEMORY_H
