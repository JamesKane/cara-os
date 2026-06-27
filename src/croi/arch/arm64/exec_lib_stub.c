// SPDX-License-Identifier: BSD-2-Clause
//
// TEMPORARY (epic H.7.7d). cara_sched's user-task spawn paths call
// Croi_ExecLib_InstallMapping to map the exec.library RX region (user VA
// 0x4000_0000) into each task's page table. That subsystem (cara_exec_lib_image
// + the lvo-gen'd vec table + trampolines) is not yet ported to AArch64, so
// this stand-in satisfies the link. It is only reached on the U-mode spawn path
// — H.7.7d exercises kernel tasks only — so returning EOK here is harmless until
// the real exec.library lands (H.7.7e+), at which point this file is deleted.

#include <cara/mm.h> // struct PageTable
#include <cara/types.h>

int Croi_ExecLib_InstallMapping(struct PageTable *pt);
int Croi_ExecLib_InstallMapping(struct PageTable *pt)
{
    (void)pt;
    return CARA_EOK;
}
