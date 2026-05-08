// SPDX-License-Identifier: BSD-2-Clause
//
// Cara MsgPort — typed messages flow between tasks through an SPSC
// ring buffer with a signal-on-edge wakeup. The ring primitive is
// cara/ring.h; the MsgPort glues it to a receiver task + a per-task
// signal bit so a blocked receiver wakes when a message arrives.
//
// Tier 2 cut: kernel-internal pointer API. The handle-based syscall
// surface (Croi_PutMsg taking a Handle, etc.) lives in user space and
// arrives with Epic K.

#ifndef CARA_MSGPORT_H
#define CARA_MSGPORT_H

#include <cara/kobj.h>
#include <cara/ring.h>
#include <cara/sched.h>
#include <cara/types.h>

struct MsgPort {
    struct Kobj         hdr;            // KOBJ_MSGPORT
    struct RingHeader  *ring;
    struct RingSlot    *slots;          // immediately follows *ring in memory
    u32                 capacity;
    struct Task        *owner;          // task signaled on empty→nonempty
    u32                 signal_bit;     // 0..31 — bit in owner->sigrecvd
};

// Create a MsgPort with `capacity` slots (must be power-of-two).
// `owner` is the task whose signal bit is fired when a message
// transitions the ring from empty to non-empty. Returns nullptr on
// allocation failure or invalid args.
[[nodiscard]] struct MsgPort *Croi_CreateMsgPort(struct Task *owner,
                                                 u32 signal_bit,
                                                 u32 capacity);

// Drop the caller's reference. When the last reference goes away
// (no open handles, no kernel pointers), the port and its ring are
// freed.
void Croi_DestroyMsgPort(struct MsgPort *p);

// Producer side. Returns true on success, false if the ring is full.
// On the empty→nonempty edge, signals the owner.
[[nodiscard]] bool Croi_PutMsg(struct MsgPort *p, struct RingSlot msg);

// Consumer side. Returns true on success, false if the ring is empty.
[[nodiscard]] bool Croi_GetMsg(struct MsgPort *p, struct RingSlot *out);

// Block until at least one message is available in `p`. Returns the
// signal mask that fired (always equals 1 << signal_bit on a normal
// wake). Must be called by p->owner.
u32 Croi_WaitPort(struct MsgPort *p);

#endif
