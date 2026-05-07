// SPDX-License-Identifier: BSD-2-Clause
//
// Cara SPSC (single-producer, single-consumer) ring buffer. Per
// docs/ARCHITECTURE.md §6, this is the primitive every Cara message-port
// builds on. The header carries head and tail on separate cache lines so
// producer and consumer don't ping-pong each other's cache. The slot array
// lives outside the header — the caller decides where it sits and how it
// is shared between sides.
//
// All operations are wait-free; signal-on-wake (whoever was idle when the
// other side made the queue non-empty) is communicated to the caller via
// the signal_needed_out flag. The actual signal delivery is the kernel's
// problem and lives outside this header.

#ifndef CARA_RING_H
#define CARA_RING_H

#include <cara/attr.h>
#include <cara/types.h>

#include <stdatomic.h>

struct CARA_ALIGNED(CARA_CACHELINE) RingHeader {
    _Atomic u32 head;                      // producer writes
    char       _pad0[CARA_CACHELINE - sizeof(_Atomic u32)];
    _Atomic u32 tail;                      // consumer writes
    char       _pad1[CARA_CACHELINE - sizeof(_Atomic u32)];
    u32        capacity;                   // power of 2
    u32        mask;                       // capacity - 1
    u64        signal_kobj;                // SIGNAL kobj id for wake; 0 if none
};

struct RingSlot {
    u32   kind;
    u32   length;
    uptr  payload;
    u64   reserved;
};

// Initialise an empty ring. capacity must be a non-zero power of two.
// signal_kobj is opaque to ring.h; the kernel uses it to identify the
// SIGNAL Kobj on which a sleeping peer is parked.
[[nodiscard]] static inline int
Ring_Init(struct RingHeader *r, u32 capacity, u64 signal_kobj)
{
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        return CARA_EINVAL;
    }
    atomic_store_explicit(&r->head, 0, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0, memory_order_relaxed);
    r->capacity = capacity;
    r->mask = capacity - 1;
    r->signal_kobj = signal_kobj;
    return CARA_EOK;
}

// Producer side. Returns true on success; false if full. signal_needed_out
// is set to true exactly when the enqueue moved the queue from empty to
// non-empty — i.e. when the consumer side may need a wake-up. May be
// nullptr if the caller doesn't care.
[[nodiscard]] static inline bool
Ring_Enqueue(struct RingHeader *r, struct RingSlot *slots,
             struct RingSlot v, bool *signal_needed_out)
{
    u32 head = atomic_load_explicit(&r->head, memory_order_relaxed);
    u32 tail = atomic_load_explicit(&r->tail, memory_order_acquire);

    if ((head - tail) >= r->capacity) {
        if (signal_needed_out) {
            *signal_needed_out = false;
        }
        return false;
    }

    slots[head & r->mask] = v;
    atomic_store_explicit(&r->head, head + 1, memory_order_release);

    if (signal_needed_out) {
        *signal_needed_out = (head == tail);
    }
    return true;
}

// Consumer side. Returns true on success; false if empty.
[[nodiscard]] static inline bool
Ring_Dequeue(struct RingHeader *r, const struct RingSlot *slots,
             struct RingSlot *out)
{
    u32 tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    u32 head = atomic_load_explicit(&r->head, memory_order_acquire);

    if (head == tail) {
        return false;
    }

    *out = slots[tail & r->mask];
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
    return true;
}

[[nodiscard]] static inline u32
Ring_Count(const struct RingHeader *r)
{
    u32 head = atomic_load_explicit(&r->head, memory_order_acquire);
    u32 tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    return head - tail;
}

[[nodiscard]] static inline bool
Ring_IsEmpty(const struct RingHeader *r)
{
    return Ring_Count(r) == 0;
}

[[nodiscard]] static inline bool
Ring_IsFull(const struct RingHeader *r)
{
    return Ring_Count(r) >= r->capacity;
}

#endif
