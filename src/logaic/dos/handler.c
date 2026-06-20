// SPDX-License-Identifier: BSD-2-Clause
//
// The Logaic dos handler — the server task behind dos.library's packet
// ops. A U-mode program's server-flavour LVO builds a DosPacket and
// PutMsg()s it here; the handler dispatches on dp_Type (ACTION_*), does
// the work, and ReplyMsg()s. This is the AmigaDOS handler/packet model
// (docs/LOGAIC_DOS.md §2.3).
//
// v0 deviation from the scoping doc: the handler runs as a *kernel-
// resident* server task rather than a U-mode Gleas. It owns the CaraFS
// mount (Croi/S-mode), so co-locating it there lets it call Carafs_*
// directly instead of bouncing every op back through a handler-only FS
// syscall. The reusable part — the U-mode server-flavour call path
// (PutMsg → server task → ReplyMsg, reused by L4/L6) — is identical
// either way, and apps can't see the seam (SASOS). A U-mode handler
// Gleas remains a future option.
//
// L3.2 wires the skeleton + one round-trip (ACTION_NIL, an echo). The
// real file/lock/dir actions (and their Carafs_* calls) land in L3.3+.

#include <cara/alloc.h>
#include <cara/carafs.h>
#include <cara/carafs_bind.h>
#include <cara/dos_lib.h>
#include <cara/exec_lib.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <exec/types.h>

// The handler's request port. Published once the handler task has
// created it; U-mode stubs fetch it via SYS_Dos_HandlerPort. nullptr
// until the handler's first run.
static struct MsgPort *g_dos_handler_port = nullptr;

// A FileLock returned to U-mode is the public head of this kernel-
// private extension: the public struct (offset 0, the BPTR target)
// plus the ExNext resume cursor and the object's own name (for Examine,
// which can't recover a name from a cnode alone). The lock is kernel-
// allocated, so its true size is ours.
struct DosLockExt {
    struct FileLock fl;            // offset 0 — the BPTR points here
    struct CarafsDirCursor cursor; // ExNext listing position
    char name[108];                // the locked object's own name (NUL-term)
};

// A FileLock stores the locked object's CaraFS cnode in fl_Key (an
// opaque BPTR-width key, docs/LOGAIC_DOS.md §4).
static u64 lock_cnode(BPTR lock)
{
    if (!lock) {
        return g_carafs.sb.root_cnode; // BNULL lock == the root
    }
    struct FileLock *fl = (struct FileLock *)BADDR(lock);
    return (u64)(uptr)fl->fl_Key;
}

// Fill a (user-supplied) FileInfoBlock from a CaraFS stat + the object's
// name. Date conversion (CaraFS ns → AmigaDOS DateStamp) is deferred —
// fib_Date is left zeroed for v0.
static void fill_fib(struct FileInfoBlock *fib, const struct CarafsStat *st, const char *name,
                     u32 name_len)
{
    unsigned char *z = (unsigned char *)fib;
    for (u32 i = 0; i < sizeof(*fib); i++) {
        z[i] = 0;
    }
    fib->fib_DiskKey = (LONG)st->cnode;
    fib->fib_DirEntryType = (st->type == CARAFS_T_DIR) ? ST_USERDIR : ST_FILE;
    fib->fib_EntryType = fib->fib_DirEntryType;
    fib->fib_Size = (LONG)st->size_bytes;
    fib->fib_NumBlocks = (LONG)st->blocks_used;
    fib->fib_Protection = (LONG)st->fib_protection;
    u32 n = name_len < sizeof(fib->fib_FileName) - 1 ? name_len : sizeof(fib->fib_FileName) - 1;
    for (u32 i = 0; i < n; i++) {
        fib->fib_FileName[i] = name[i];
    }
    fib->fib_FileName[n] = 0;
}

// Resolve `path` (an AmigaDOS name) relative to the `base` directory
// cnode into a target cnode. A leading volume prefix ("X:") resets to
// root; remaining '/'-separated components are walked with DirLookup.
// An empty path resolves to `base`. CARA_ENOENT on a missing component.
static int dos_resolve(const char *path, u64 base, u64 *out, char *namebuf, u32 namecap)
{
    u64 cur = base;
    const char *p = path;
    const char *last = ""; // final component (for the lock's own name)
    u32 last_len = 0;
    for (const char *q = path; q && *q; q++) {
        if (*q == ':') {
            cur = g_carafs.sb.root_cnode;
            p = q + 1;
            break;
        }
    }
    while (p && *p) {
        const char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        u32 len = (u32)(p - start);
        if (*p == '/') {
            p++;
        }
        if (len == 0) {
            continue;
        }
        u64 c;
        u16 t;
        if (Carafs_DirLookup(&g_carafs, cur, start, len, &c, &t) != CARA_EOK) {
            return CARA_ENOENT;
        }
        cur = c;
        last = start;
        last_len = len;
    }
    if (namebuf && namecap) {
        u32 n = last_len < namecap - 1 ? last_len : namecap - 1;
        for (u32 i = 0; i < n; i++) {
            namebuf[i] = last[i];
        }
        namebuf[n] = 0;
    }
    *out = cur;
    return CARA_EOK;
}

// Shared dispatch for dos packet ops — called both by the handler task
// (on GetMsg) and directly by the `syscall`-flavour dos LVO impls (the
// fast path: CaraFS ops are synchronous and the cooperative single-hart
// scheduler makes a syscall atomic, so no round-trip is needed for
// correctness; the handler task exists for the U-mode PutMsg path and
// future async/SMP). Reads/writes g_carafs (Croi/S-mode).
void Croi_Dos_Dispatch(struct DosPacket *dp)
{
    switch (dp->dp_Type) {
    case ACTION_NIL:
        // Round-trip self-test: echo dp_Arg1.
        dp->dp_Res1 = dp->dp_Arg1;
        dp->dp_Res2 = 0;
        break;

    case ACTION_LOCATE_OBJECT: {
        // dp_Arg1 = base lock (BPTR, 0 = root), dp_Arg2 = name (C str),
        // dp_Arg3 = mode. Returns a FileLock BPTR or 0.
        const char *name = (const char *)(uptr)dp->dp_Arg2;
        u64 base = lock_cnode((BPTR)(uptr)dp->dp_Arg1);
        struct DosLockExt *ext = (struct DosLockExt *)Croi_AllocShared(sizeof(struct DosLockExt));
        if (!ext) {
            dp->dp_Res1 = 0;
            dp->dp_Res2 = ERROR_NO_FREE_STORE;
            break;
        }
        u64 cnode;
        if (dos_resolve(name, base, &cnode, ext->name, sizeof(ext->name)) != CARA_EOK) {
            Croi_Free(ext);
            dp->dp_Res1 = 0;
            dp->dp_Res2 = ERROR_OBJECT_NOT_FOUND;
            break;
        }
        ext->fl.fl_Link = BNULL;
        ext->fl.fl_Key = (BPTR)(uptr)cnode;
        ext->fl.fl_Access = (LONG)dp->dp_Arg3;
        ext->fl.fl_Task = g_dos_handler_port;
        ext->fl.fl_Volume = BNULL;
        ext->cursor = (struct CarafsDirCursor){ 0 };
        dp->dp_Res1 = (SIPTR)(uptr)MKBADDR(&ext->fl);
        dp->dp_Res2 = 0;
        break;
    }

    case ACTION_EXAMINE_OBJECT: {
        // dp_Arg1 = lock BPTR, dp_Arg2 = FileInfoBlock *. Stats the
        // locked object and resets its cursor so a following ExNext
        // lists from the start (for a directory lock).
        BPTR lock = (BPTR)(uptr)dp->dp_Arg1;
        struct FileInfoBlock *fib = (struct FileInfoBlock *)(uptr)dp->dp_Arg2;
        struct CarafsStat st;
        if (!fib || Carafs_CnodeStat(&g_carafs, lock_cnode(lock), &st) != CARA_EOK) {
            dp->dp_Res1 = DOSFALSE;
            dp->dp_Res2 = ERROR_OBJECT_NOT_FOUND;
            break;
        }
        const char *nm = "";
        u32 nl = 0;
        if (lock) {
            struct DosLockExt *ext = (struct DosLockExt *)BADDR(lock);
            nm = ext->name;
            while (ext->name[nl]) {
                nl++;
            }
            ext->cursor = (struct CarafsDirCursor){ 0 }; // restart ExNext
        }
        fill_fib(fib, &st, nm, nl);
        dp->dp_Res1 = DOSTRUE;
        dp->dp_Res2 = 0;
        break;
    }

    case ACTION_EXAMINE_NEXT: {
        // dp_Arg1 = dir lock BPTR, dp_Arg2 = FileInfoBlock *. Advances
        // the lock's cursor; DOSFALSE + ERROR_NO_MORE_ENTRIES past the end.
        BPTR lock = (BPTR)(uptr)dp->dp_Arg1;
        struct FileInfoBlock *fib = (struct FileInfoBlock *)(uptr)dp->dp_Arg2;
        if (!lock || !fib) {
            dp->dp_Res1 = DOSFALSE;
            dp->dp_Res2 = ERROR_OBJECT_WRONG_TYPE;
            break;
        }
        struct DosLockExt *ext = (struct DosLockExt *)BADDR(lock);
        struct CarafsDirEntry e;
        int rc = Carafs_DirNext(&g_carafs, lock_cnode(lock), &ext->cursor, &e);
        if (rc != CARA_EOK) {
            dp->dp_Res1 = DOSFALSE;
            dp->dp_Res2 = ERROR_NO_MORE_ENTRIES;
            break;
        }
        struct CarafsStat st;
        if (Carafs_CnodeStat(&g_carafs, e.cnode, &st) != CARA_EOK) {
            dp->dp_Res1 = DOSFALSE;
            dp->dp_Res2 = ERROR_OBJECT_NOT_FOUND;
            break;
        }
        fill_fib(fib, &st, (const char *)e.name, e.name_len);
        dp->dp_Res1 = DOSTRUE;
        dp->dp_Res2 = 0;
        break;
    }

    case ACTION_FREE_LOCK: {
        // dp_Arg1 = lock BPTR. Frees it; BNULL is a no-op (== root).
        BPTR lock = (BPTR)(uptr)dp->dp_Arg1;
        if (lock) {
            Croi_Free(BADDR(lock));
        }
        dp->dp_Res1 = DOSTRUE;
        dp->dp_Res2 = 0;
        break;
    }

    default:
        // Unimplemented action (more land in L3.4+). Fail cleanly so a
        // caller never hangs.
        dp->dp_Res1 = DOSFALSE;
        dp->dp_Res2 = ERROR_OBJECT_WRONG_TYPE;
        break;
    }
}

[[noreturn]] static void dos_handler_task(void *arg)
{
    (void)arg;
    g_dos_handler_port = Croi_CreateMsgPort_Impl();
    if (!g_dos_handler_port) {
        LOG_FATAL("dosh", "dos.handler: CreateMsgPort failed");
    }
    LOG_INFO("dosh", "dos.handler up (port=%p)", (void *)g_dos_handler_port);

    for (;;) {
        (void)Croi_WaitPort_Impl(g_dos_handler_port);
        struct Message *m;
        while ((m = Croi_GetMsg_Impl(g_dos_handler_port)) != nullptr) {
            // AmigaDOS convention: the Message's ln_Name is the DosPacket.
            struct DosPacket *dp = (struct DosPacket *)m->mn_Node.ln_Name;
            if (dp) {
                Croi_Dos_Dispatch(dp);
            }
            Croi_ReplyMsg_Impl(m);
        }
    }
}

void Croi_Dos_StartHandler(void)
{
    struct Task *t = Croi_SpawnKernelTask("dos.handler", 120, dos_handler_task, nullptr);
    if (!t) {
        LOG_FATAL("dosh", "dos.handler spawn failed");
        return;
    }
    // Yield so the handler runs to its first WaitPort — creating and
    // publishing its port — before anyone (tests / Gleasanna) asks for
    // it. It has higher priority than kmain, so this single yield runs
    // it up to the blocking WaitPort, then control returns here.
    Croi_Yield();
}

struct MsgPort *Croi_Dos_HandlerPort_Impl(void)
{
    return g_dos_handler_port;
}
