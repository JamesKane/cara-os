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

// Examine(lock, fib) → BOOL. Stat the locked object into fib (and reset
// the lock's cursor for ExNext).
LONG Croi_Dos_Examine_Impl(BPTR lock, struct FileInfoBlock *fib)
{
    struct DosPacket dp = { 0 };
    dp.dp_Type = ACTION_EXAMINE_OBJECT;
    dp.dp_Arg1 = (SIPTR)(uptr)lock;
    dp.dp_Arg2 = (SIPTR)(uptr)fib;
    Croi_Dos_Dispatch(&dp);
    set_ioerr(dp.dp_Res2);
    return (LONG)dp.dp_Res1;
}

// ExNext(lock, fib) → BOOL. Fill fib with the next directory entry;
// DOSFALSE at end-of-listing (IoErr ERROR_NO_MORE_ENTRIES).
LONG Croi_Dos_ExNext_Impl(BPTR lock, struct FileInfoBlock *fib)
{
    struct DosPacket dp = { 0 };
    dp.dp_Type = ACTION_EXAMINE_NEXT;
    dp.dp_Arg1 = (SIPTR)(uptr)lock;
    dp.dp_Arg2 = (SIPTR)(uptr)fib;
    Croi_Dos_Dispatch(&dp);
    set_ioerr(dp.dp_Res2);
    return (LONG)dp.dp_Res1;
}

// Open(name, accessMode) → BPTR FileHandle (0 on failure). The mode
// selects the FIND action.
BPTR Croi_Dos_Open_Impl(STRPTR name, LONG accessMode)
{
    struct Process *p = (struct Process *)Sched_Current();
    struct DosPacket dp = { 0 };
    dp.dp_Type = (accessMode == MODE_NEWFILE)     ? ACTION_FINDOUTPUT
                 : (accessMode == MODE_READWRITE) ? ACTION_FINDUPDATE
                                                  : ACTION_FINDINPUT; // MODE_OLDFILE
    dp.dp_Arg1 = (SIPTR)(uptr)(p ? p->pr_CurrentDir : BNULL);
    dp.dp_Arg2 = (SIPTR)(uptr)name;
    Croi_Dos_Dispatch(&dp);
    set_ioerr(dp.dp_Res2);
    return (BPTR)(uptr)dp.dp_Res1;
}

// Close(file) → BOOL.
LONG Croi_Dos_Close_Impl(BPTR file)
{
    struct DosPacket dp = { 0 };
    dp.dp_Type = ACTION_END;
    dp.dp_Arg1 = (SIPTR)(uptr)file;
    Croi_Dos_Dispatch(&dp);
    return (LONG)dp.dp_Res1;
}

// Read(file, buffer, length) → bytes read (0 EOF, -1 error).
LONG Croi_Dos_Read_Impl(BPTR file, APTR buffer, LONG length)
{
    struct DosPacket dp = { 0 };
    dp.dp_Type = ACTION_READ;
    dp.dp_Arg1 = (SIPTR)(uptr)file;
    dp.dp_Arg2 = (SIPTR)(uptr)buffer;
    dp.dp_Arg3 = (SIPTR)length;
    Croi_Dos_Dispatch(&dp);
    set_ioerr(dp.dp_Res2);
    return (LONG)dp.dp_Res1;
}

// Write(file, buffer, length) → bytes written (-1 error).
LONG Croi_Dos_Write_Impl(BPTR file, APTR buffer, LONG length)
{
    struct DosPacket dp = { 0 };
    dp.dp_Type = ACTION_WRITE;
    dp.dp_Arg1 = (SIPTR)(uptr)file;
    dp.dp_Arg2 = (SIPTR)(uptr)buffer;
    dp.dp_Arg3 = (SIPTR)length;
    Croi_Dos_Dispatch(&dp);
    set_ioerr(dp.dp_Res2);
    return (LONG)dp.dp_Res1;
}

// Seek(file, position, mode) → previous position (-1 error).
LONG Croi_Dos_Seek_Impl(BPTR file, LONG position, LONG mode)
{
    struct DosPacket dp = { 0 };
    dp.dp_Type = ACTION_SEEK;
    dp.dp_Arg1 = (SIPTR)(uptr)file;
    dp.dp_Arg2 = (SIPTR)position;
    dp.dp_Arg3 = (SIPTR)mode;
    Croi_Dos_Dispatch(&dp);
    set_ioerr(dp.dp_Res2);
    return (LONG)dp.dp_Res1;
}
