// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-internal MsgPort impl. The public V36+ struct MsgPort comes
// from <exec/ports.h>; CaraOS extends it with the SPSC ring and the
// signal-on-edge bookkeeping.
//
// The pub field is first so a `struct CroiMsgPort *` is interchangeable
// with a `struct MsgPort *` via plain cast (the public API hands out
// the latter; the kernel internal API uses the former). Going the other
// way — public MsgPort * back to CroiMsgPort * — uses container_of
// against the pub member; exec.library's PutMsg/GetMsg/WaitPort LVO
// bodies (Phase C) do exactly that.

#ifndef CROI_IPC_MSGPORT_IMPL_H
#define CROI_IPC_MSGPORT_IMPL_H

#include <cara/kobj.h>
#include <cara/ring.h>
#include <cara/types.h>
#include <exec/ports.h>

struct Task;

struct CroiMsgPort {
    struct MsgPort pub; // V36+ public part — first field
    struct Kobj hdr;    // KOBJ_MSGPORT
    struct RingHeader *ring;
    struct RingSlot *slots;
    u32 capacity;
    struct Task *owner;
    u32 signal_bit;
};

#endif
