// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-internal MsgPort API. Operates on `struct CroiMsgPort` —
// the V36+-public `struct MsgPort` (in <exec/ports.h>) extended with
// the SPSC ring + signal-on-edge bookkeeping. The struct definition
// itself is private to src/croi/ipc/msgport_impl.h; this header only
// publishes the brand-namespace function prototypes.
//
// Phase C will publish exec.library's V36+ PutMsg/GetMsg/WaitPort
// LVOs as thin wrappers that container_of-down from `struct MsgPort *`
// to `struct CroiMsgPort *` and then call into the helpers below.

#ifndef CARA_MSGPORT_H
#define CARA_MSGPORT_H

#include <cara/ring.h>
#include <cara/types.h>

struct CroiMsgPort;
struct Task;

[[nodiscard]] struct CroiMsgPort *Croi_CreateMsgPort(struct Task *owner, u32 signal_bit,
                                                     u32 capacity);

void Croi_DestroyMsgPort(struct CroiMsgPort *p);

[[nodiscard]] bool Croi_PutMsg(struct CroiMsgPort *p, struct RingSlot msg);

[[nodiscard]] bool Croi_GetMsg(struct CroiMsgPort *p, struct RingSlot *out);

u32 Croi_WaitPort(struct CroiMsgPort *p);

#endif
