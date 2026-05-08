// SPDX-License-Identifier: BSD-2-Clause
//
// MsgPort implementation. Allocates RingHeader + slot array in a
// single heap block (the heap returns sufficiently aligned pointers
// for our cache-line-padded RingHeader layout — small enough requests
// land in the slab whose object size is a multiple of CARA_CACHELINE,
// large enough requests are page-aligned).

#include <cara/alloc.h>
#include <cara/kobj.h>
#include <cara/log.h>
#include <cara/msgport.h>
#include <cara/ring.h>
#include <cara/sched.h>
#include <cara/types.h>

static void msgport_destroy(struct Kobj *k)
{
    struct MsgPort *p = (struct MsgPort *)k;
    if (p->ring) {
        Croi_Free(p->ring);
    }
    Croi_Free(p);
}

[[nodiscard]] struct MsgPort *Croi_CreateMsgPort(struct Task *owner,
                                                 u32 signal_bit, u32 capacity)
{
    if (!owner || signal_bit >= 32 || capacity == 0) {
        return nullptr;
    }
    if ((capacity & (capacity - 1)) != 0) {
        return nullptr;
    }

    struct MsgPort *p = (struct MsgPort *)Croi_Alloc(sizeof(struct MsgPort));
    if (!p) {
        return nullptr;
    }
    Kobj_Init(&p->hdr, KOBJ_MSGPORT, msgport_destroy);

    usize ring_bytes = sizeof(struct RingHeader)
                       + (usize)capacity * sizeof(struct RingSlot);
    void *block = Croi_Alloc(ring_bytes);
    if (!block) {
        Croi_Free(p);
        return nullptr;
    }
    p->ring = (struct RingHeader *)block;
    p->slots = (struct RingSlot *)((u8 *)block + sizeof(struct RingHeader));
    p->capacity = capacity;
    p->owner = owner;
    p->signal_bit = signal_bit;

    if (Ring_Init(p->ring, capacity, /*signal_kobj=*/0) != CARA_EOK) {
        Croi_Free(block);
        Croi_Free(p);
        return nullptr;
    }
    return p;
}

void Croi_DestroyMsgPort(struct MsgPort *p)
{
    if (!p) {
        return;
    }
    Kobj_Release(&p->hdr);
}

[[nodiscard]] bool Croi_PutMsg(struct MsgPort *p, struct RingSlot msg)
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

[[nodiscard]] bool Croi_GetMsg(struct MsgPort *p, struct RingSlot *out)
{
    if (!p || !out) {
        return false;
    }
    return Ring_Dequeue(p->ring, p->slots, out);
}

u32 Croi_WaitPort(struct MsgPort *p)
{
    if (!p) {
        return 0;
    }
    return Croi_Wait(1u << p->signal_bit);
}
