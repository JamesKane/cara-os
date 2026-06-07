// SPDX-License-Identifier: BSD-2-Clause
//
// SASOS shared system heap (ARCHITECTURE.md §4.3 / §4.4).
//
// A fixed arena of physical RAM is mapped RW+U into the lower-half VA
// window at CARA_SHARED_VA_BASE, via a single shared L2 page-table entry
// installed into the boot PT and every task root (the §4.4 shared-
// top-level mechanism — the L1 subtree under that entry is one object
// shared by all page tables, so any mapping in the window is visible
// everywhere). Because _start.S sets sstatus.SUM, the kernel can also
// dereference these U pages, so a pointer returned from a library
// syscall (a Screen, Window, IntuiMessage, …) is usable verbatim in
// U-mode — the SASOS payoff that makes zero-copy IPC and library bases
// work. Croi_AllocShared allocates here; Croi_Free auto-routes by range.

#ifndef CARA_SHARED_H
#define CARA_SHARED_H

#include <cara/mm.h>
#include <cara/types.h>

// Lower-half window base — 4 GiB, per ARCHITECTURE.md §4.3. Its Sv39 L2
// index is VA_BASE >> 30; the shared subtree hangs off that one slot.
constexpr u64 CARA_SHARED_VA_BASE = 0x0000000100000000ull;
constexpr u32 CARA_SHARED_L2_INDEX = 4; // (CARA_SHARED_VA_BASE >> 30) & 0x1FF

// Phase 1 reserves a modest fixed arena; it is growable (more L2 slots /
// lazy mapping) when a workload needs it. 8 MiB is ample for Clar plus a
// handful of libraries' Screen/Window/Gadget/MsgPort/IntuiMessage churn.
constexpr u64 CARA_SHARED_ARENA_BYTES = (8ull * 1024 * 1024);

// Initialise the shared heap from the main page allocator: reserve the
// arena, allocate the shared L1 subtree, map the arena RW+U into the
// window, install the shared L2 entry into the boot PT, and register the
// heap with the allocator. Call once at boot after Heap_SetActive.
[[nodiscard]] int Croi_Shared_Init(struct PageAllocator *pa);

// Install the shared L2 entry into a freshly-built task page table so the
// task reaches the shared window. Called from the user-task spawn path.
[[nodiscard]] int Croi_Shared_InstallMapping(struct PageTable *pt);

// Allocate `size` bytes from the shared heap. Returns a lower-half RW+U
// VA both kernel and user mode can dereference, or nullptr. Free with the
// ordinary Croi_Free (it routes shared-window pointers back here).
[[nodiscard]] void *Croi_AllocShared(usize size);

#endif
