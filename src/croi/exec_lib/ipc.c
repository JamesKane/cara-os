// SPDX-License-Identifier: BSD-2-Clause
//
// V36+ PutMsg / GetMsg / WaitPort over CaraOS's SPSC ring + signal
// MsgPort. Each impl recovers the kernel-internal CroiMsgPort wrapper
// from the public V36+ MsgPort pointer via container_of (the wrapper's
// first field is the public struct).
//
// Message payload model: the V36+ caller passes a `struct Message *`
// to PutMsg. CaraOS's ring carries a RingSlot per slot — a fixed-size
// record. We pack the SASOS pointer of the V36+ Message into the
// RingSlot's `payload` field; GetMsg / WaitPort recovers it. The
// caller's struct stays where it was allocated (SASOS — every task
// sees the same pointer), so this is zero-copy.
//
// V36+ WaitPort returns the first message *without* dequeuing it; the
// caller then GetMsg's to take it. Our ring doesn't peek, so v0
// WaitPort returns nullptr — programs that rely on the return value
// must use GetMsg after WaitPort. Documented gap; revisit if a real
// V36+ program needs the peek behaviour.

#include "../ipc/msgport_impl.h"

#include <cara/exec_lib.h>
#include <cara/log.h>
#include <cara/msgport.h>
#include <cara/ring.h>
#include <cara/types.h>
#include <exec/nodes.h>
#include <exec/ports.h>

#include <stddef.h>

static struct CroiMsgPort *port_to_croi(struct MsgPort *port)
{
    return (struct CroiMsgPort *)((char *)port
                                  - offsetof(struct CroiMsgPort, pub));
}

void Croi_PutMsg_Impl(struct MsgPort *port, struct Message *msg)
{
    if (!port || !msg) {
        return;
    }
    struct CroiMsgPort *cmp = port_to_croi(port);
    struct RingSlot slot = {
        .kind     = NT_MESSAGE,
        .length   = msg->mn_Length,
        .payload  = (uptr)msg,
        .reserved = 0,
    };
    if (!Croi_PutMsg(cmp, slot)) {
        // Ring is full. V36+ PutMsg has no return value, so the caller
        // gets no back-pressure — log and drop.
        LOG_WARN("port", "PutMsg dropped: ring full");
    }
}

struct Message *Croi_GetMsg_Impl(struct MsgPort *port)
{
    if (!port) {
        return nullptr;
    }
    struct CroiMsgPort *cmp = port_to_croi(port);
    struct RingSlot slot = { 0 };
    if (!Croi_GetMsg(cmp, &slot)) {
        return nullptr;
    }
    return (struct Message *)(uptr)slot.payload;
}

struct Message *Croi_WaitPort_Impl(struct MsgPort *port)
{
    if (!port) {
        return nullptr;
    }
    struct CroiMsgPort *cmp = port_to_croi(port);
    (void)Croi_WaitPort(cmp);
    // v0: return nullptr — see header note. Caller must GetMsg next.
    return nullptr;
}
