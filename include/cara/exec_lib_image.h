// SPDX-License-Identifier: BSD-2-Clause
//
// Layout helpers for the exec.library shared region (croi.lds
// `.exec_lib` section, VMA = 0x4000_0000, LMA flowing in RAM_PHYS).
//
// The library has two views:
//
//   - User view  — VMA 0x4000_0000+, mapped R+X+U into every user
//                  task's PT by Croi_ExecLib_InstallMapping. User
//                  code's <proto/exec.h> inline stubs dereference
//                  this view via the SysBase global.
//
//   - Kernel view — upper-half direct map at
//                  Mm_PhysToVirt(__exec_lib_lma_start). Croi_MakeLibrary
//                  populates struct Library / vec table through this
//                  view (the kernel's own page table doesn't map
//                  user-half VAs). Returned by Croi_ExecLib_KernelView.
//
// Both views are aliases of the same physical pages; writes via one
// are observed via the other after a coherent fence.

#ifndef CARA_EXEC_LIB_IMAGE_H
#define CARA_EXEC_LIB_IMAGE_H

#include <cara/types.h>

// Linker-emitted literal pool of LMA values (in .kernel_extents,
// upper-half rodata — PC-relative reachable from kernel C code).
// Pattern matches _kernel_image_phys_start / _kernel_image_phys_end.
extern u64 _exec_lib_image_lma_start;   // physical address of section start
extern u64 _exec_lib_image_lma_size;    // bytes

// User-VA constants — must match the linker layout in src/croi/croi.lds.
#define CARA_EXEC_LIB_USER_VA           0x0000000040000000ull
#define CARA_EXEC_LIB_USER_BASE_OFFSET  0x800ull
#define CARA_EXEC_LIB_USER_BASE \
    (CARA_EXEC_LIB_USER_VA + CARA_EXEC_LIB_USER_BASE_OFFSET)

struct Library;
struct PageTable;

// Map the .exec_lib physical pages into `pt` at user VA 0x4000_0000+
// with PTE_USER_RWX. Called from every user-task spawn path so user
// code can read+execute the vec table and trampolines AND so kernel
// code running with that PT (via SUM=1) can update lib_OpenCnt.
[[nodiscard]] int Croi_ExecLib_InstallMapping(struct PageTable *pt);

// Install the same mapping in the boot PT (the one kmain runs with
// before any user task is scheduled). Reads SATP to locate the
// current PT root. Called once at boot before Croi_MakeLibrary.
[[nodiscard]] int Croi_ExecLib_InstallInBootPT(void);

// Return the kernel-side (upper-half direct-map) pointer to the
// exec.library struct Library. Used by tests that want to verify
// the kernel-direct-map view; the construction code now writes
// through user-VA 0x4000_0800 directly thanks to InstallInBootPT.
struct Library *Croi_ExecLib_KernelView(void);

// Bytes available in the positive-side region past struct Library
// (for ExecBase-style private state). Computed from the linker
// reservation in src/croi/croi.lds.
usize Croi_ExecLib_PrivateSize(void);

#endif
