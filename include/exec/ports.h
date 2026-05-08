// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ MsgPort and Message — the public IPC ABI.
//
// PutMsg / GetMsg / WaitPort / ReplyMsg / FindPort / AddPort /
// RemPort all operate on these. CaraOS preserves the public layouts
// verbatim (DRIFT_2026-05.md H4 / H6); the kernel's ring + signal
// machinery hangs off the brand-namespace `struct CroiMsgPort`
// (src/croi/ipc/msgport_impl.h) which extends the public MsgPort by
// appending its private fields after the visible portion. User code
// only ever sees `struct MsgPort *`.

#ifndef EXEC_PORTS_H
#define EXEC_PORTS_H

#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/types.h>

struct MsgPort {
    struct Node  mp_Node;
    UBYTE        mp_Flags;
    UBYTE        mp_SigBit;       // signal bit number (0..31), not a mask
    void        *mp_SigTask;      // struct Task * (PA_SIGNAL) or struct Interrupt * (PA_SOFTINT)
    struct List  mp_MsgList;
};

struct Message {
    struct Node     mn_Node;
    struct MsgPort *mn_ReplyPort;
    UWORD           mn_Length;    // total length, including this header
};

// V36+ mp_Flags — PF_ACTION holds the arrival-action selector.
#define PF_ACTION   3              // mask covering the action bits
#define PA_SIGNAL   0              // signal mp_SigTask on arrival
#define PA_SOFTINT  1              // cause a software interrupt
#define PA_IGNORE   2              // arrival is silent

#define IsMsgPortEmpty(p) \
    (((struct List *)&((p)->mp_MsgList))->lh_TailPred \
        == (struct Node *)&((p)->mp_MsgList))

#endif // EXEC_PORTS_H
