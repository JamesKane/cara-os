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

// Resolve `path` (an AmigaDOS name) relative to the `base` directory
// cnode into a target cnode. A leading volume prefix ("X:") resets to
// root; remaining '/'-separated components are walked with DirLookup.
// An empty path resolves to `base`. CARA_ENOENT on a missing component.
static int dos_resolve(const char *path, u64 base, u64 *out)
{
    u64 cur = base;
    const char *p = path;
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
        u64 cnode;
        if (dos_resolve(name, base, &cnode) != CARA_EOK) {
            dp->dp_Res1 = 0;
            dp->dp_Res2 = ERROR_OBJECT_NOT_FOUND;
            break;
        }
        struct FileLock *fl = (struct FileLock *)Croi_AllocShared(sizeof(struct FileLock));
        if (!fl) {
            dp->dp_Res1 = 0;
            dp->dp_Res2 = ERROR_NO_FREE_STORE;
            break;
        }
        fl->fl_Link = BNULL;
        fl->fl_Key = (BPTR)(uptr)cnode;
        fl->fl_Access = (LONG)dp->dp_Arg3;
        fl->fl_Task = g_dos_handler_port;
        fl->fl_Volume = BNULL;
        dp->dp_Res1 = (SIPTR)(uptr)MKBADDR(fl);
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
