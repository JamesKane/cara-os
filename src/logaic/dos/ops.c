// SPDX-License-Identifier: BSD-2-Clause
//
// dos.library lock LVOs (L3.3) — Lock / UnLock / DupLock / CurrentDir.
// `syscall` flavour: the U-mode trampoline ecalls here, and the
// FS-touching ops (Lock/UnLock) build a DosPacket and call the shared
// Croi_Dos_Dispatch (the handler's dispatch). The pure-memory ops
// (DupLock, CurrentDir) act directly on the caller's Process / the
// shared-heap FileLock. See docs/LOGAIC_DOS.md §4.

#include <cara/dos_lib.h>
#include <cara/sched.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/types.h>

// Record IoErr (pr_Result2) on the calling Process from a packet result.
static void set_ioerr(SIPTR code)
{
    struct Process *p = (struct Process *)Sched_Current();
    if (p) {
        p->pr_Result2 = (LONG)code;
    }
}

// Lock(name, accessMode) → BPTR FileLock (0 on failure; IoErr set).
BPTR Croi_Dos_Lock_Impl(STRPTR name, LONG accessMode)
{
    struct Process *p = (struct Process *)Sched_Current();
    struct DosPacket dp = { 0 };
    dp.dp_Type = ACTION_LOCATE_OBJECT;
    dp.dp_Arg1 = (SIPTR)(uptr)(p ? p->pr_CurrentDir : BNULL);
    dp.dp_Arg2 = (SIPTR)(uptr)name;
    dp.dp_Arg3 = (SIPTR)accessMode;
    Croi_Dos_Dispatch(&dp);
    set_ioerr(dp.dp_Res2);
    return (BPTR)(uptr)dp.dp_Res1;
}

// UnLock(lock) — release a FileLock. BNULL is a no-op.
void Croi_Dos_UnLock_Impl(BPTR lock)
{
    struct DosPacket dp = { 0 };
    dp.dp_Type = ACTION_FREE_LOCK;
    dp.dp_Arg1 = (SIPTR)(uptr)lock;
    Croi_Dos_Dispatch(&dp);
}

// DupLock(lock) → a second shared lock on the same object (pure memory).
BPTR Croi_Dos_DupLock_Impl(BPTR lock)
{
    if (!lock) {
        return BNULL;
    }
    struct FileLock *src = (struct FileLock *)BADDR(lock);
    struct FileLock *dup = (struct FileLock *)Croi_AllocShared(sizeof(struct FileLock));
    if (!dup) {
        set_ioerr(ERROR_NO_FREE_STORE);
        return BNULL;
    }
    *dup = *src;
    dup->fl_Link = BNULL;
    return MKBADDR(dup);
}

// CurrentDir(lock) — install lock as the Process's current directory,
// returning the previous one. Library-local: no FS, no packet.
BPTR Croi_Dos_CurrentDir_Impl(BPTR lock)
{
    struct Process *p = (struct Process *)Sched_Current();
    if (!p) {
        return BNULL;
    }
    BPTR old = p->pr_CurrentDir;
    p->pr_CurrentDir = lock;
    return old;
}
