// SPDX-License-Identifier: BSD-2-Clause
//
// dos.library IoErr() — the `syscall`-flavour impl. Returns the calling
// Process's pr_Result2. Because every U-mode Gleas's struct Task is
// embedded at the front of a struct Process in the SASOS shared heap
// (docs/LOGAIC_DOS.md §2.2), Sched_Current() is castable to
// struct Process *. Syscalls only ever arrive from U-mode tasks (an
// ecall from a Gleas), so the current task is always a real Process.

#include <cara/dos_lib.h>
#include <cara/sched.h>
#include <cara/types.h>
#include <dos/dosextens.h>
#include <exec/types.h>

LONG Croi_Dos_IoErr_Impl(void)
{
    struct Process *p = (struct Process *)Sched_Current();
    if (!p) {
        return 0;
    }
    return p->pr_Result2;
}
