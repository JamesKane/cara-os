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

#include <cara/dos_lib.h>
#include <cara/exec_lib.h>
#include <cara/log.h>
#include <cara/sched.h>
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

static void dos_dispatch(struct DosPacket *dp)
{
    switch (dp->dp_Type) {
    case ACTION_NIL:
        // Skeleton round-trip proof: echo dp_Arg1 back as the result.
        dp->dp_Res1 = dp->dp_Arg1;
        dp->dp_Res2 = 0;
        break;
    default:
        // Unimplemented action (the real ACTION_* handlers land in
        // L3.3+). Fail cleanly so a caller never hangs.
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
                dos_dispatch(dp);
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
