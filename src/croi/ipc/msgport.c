// SPDX-License-Identifier: BSD-2-Clause
//
// MsgPort implementation. Allocates a CroiMsgPort wrapper plus the
// RingHeader + slot array. The wrapper places the V36+ public part
// (struct MsgPort pub) first so a CroiMsgPort * is interchangeable
// with a struct MsgPort * via plain cast; PutMsg/GetMsg LVO bodies
// (Phase C) recover the wrapper from a public pointer via
// container_of(msgport, struct CroiMsgPort, pub).

#include <cara/alloc.h>
#include <cara/kobj.h>
#include <cara/log.h>
#include <cara/msgport.h>
#include <cara/ring.h>
#include <cara/sched.h>
#include <cara/shared.h>
#include <cara/types.h>

#include "msgport_impl.h"

#include <stddef.h> // offsetof

static struct CroiMsgPort *kobj_to_port(struct Kobj *k)
{
    return (struct CroiMsgPort *)((char *)k - offsetof(struct CroiMsgPort, hdr));
}

static void msgport_destroy(struct Kobj *k)
{
    struct CroiMsgPort *p = kobj_to_port(k);
    if (p->ring) {
        Croi_Free(p->ring);
    }
    Croi_Free(p);
}

[[nodiscard]] struct CroiMsgPort *Croi_CreateMsgPort(struct Task *owner, u32 signal_bit,
                                                     u32 capacity)
{
    if (!owner || signal_bit >= 32 || capacity == 0) {
        return nullptr;
    }
    if ((capacity & (capacity - 1)) != 0) {
        return nullptr;
    }

    // A V36+ MsgPort is public memory: a window's UserPort must be
    // U-mode-readable (e.g. Clar reads mp_SigBit to Wait() across several
    // ports), so allocate the wrapper + ring from the SASOS shared heap.
    // Croi_Free range-routes both back here.
    struct CroiMsgPort *p = (struct CroiMsgPort *)Croi_AllocShared(sizeof(struct CroiMsgPort));
    if (!p) {
        return nullptr;
    }
    *p = (struct CroiMsgPort){ 0 };
    Kobj_Init(&p->hdr, KOBJ_MSGPORT, msgport_destroy);

    usize ring_bytes = sizeof(struct RingHeader) + (usize)capacity * sizeof(struct RingSlot);
    void *block = Croi_AllocShared(ring_bytes);
    if (!block) {
        Croi_Free(p);
        return nullptr;
    }
    p->ring = (struct RingHeader *)block;
    p->slots = (struct RingSlot *)((u8 *)block + sizeof(struct RingHeader));
    p->capacity = capacity;
    p->owner = owner;
    p->signal_bit = signal_bit;

    // Populate the V36+ public MsgPort fields so user code (e.g. Clar)
    // can read mp_SigBit to build a Wait() mask across several ports,
    // and so the port reads as a proper PA_SIGNAL port.
    p->pub.mp_Node.ln_Type = NT_MSGPORT;
    p->pub.mp_Flags = PA_SIGNAL;
    p->pub.mp_SigBit = (UBYTE)signal_bit;
    p->pub.mp_SigTask = owner;

    if (Ring_Init(p->ring, capacity, /*signal_kobj=*/0) != CARA_EOK) {
        Croi_Free(block);
        Croi_Free(p);
        return nullptr;
    }
    return p;
}

void Croi_DestroyMsgPort(struct CroiMsgPort *p)
{
    if (!p) {
        return;
    }
    Kobj_Release(&p->hdr);
}

[[nodiscard]] bool Croi_PutMsg(struct CroiMsgPort *p, struct RingSlot msg)
{
    if (!p) {
        return false;
    }
    bool need_signal = false;
    if (!Ring_Enqueue(p->ring, p->slots, msg, &need_signal)) {
        return false;
    }
    if (need_signal) {
        Croi_Signal(p->owner, 1u << p->signal_bit);
    }
    return true;
}

[[nodiscard]] bool Croi_GetMsg(struct CroiMsgPort *p, struct RingSlot *out)
{
    if (!p || !out) {
        return false;
    }
    return Ring_Dequeue(p->ring, p->slots, out);
}

u32 Croi_WaitPort(struct CroiMsgPort *p)
{
    if (!p) {
        return 0;
    }
    return Croi_Wait(1u << p->signal_bit);
}
