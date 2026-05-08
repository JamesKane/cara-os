// SPDX-License-Identifier: BSD-2-Clause
//
// Kobj base — type-tagged refcounted kernel objects with an optional
// destroy callback. Refcount is atomic so cross-hart sharing works
// when SMP arrives in Epic H without revisiting the lifecycle code.

#include <cara/kobj.h>
#include <cara/types.h>

#include <stdatomic.h>

static _Atomic u64 g_next_kobj_id = 1;

void Kobj_Init(struct Kobj *k, KobjType type, void (*destroy)(struct Kobj *))
{
    if (!k) {
        return;
    }
    k->type = type;
    k->flags = 0;
    atomic_store_explicit(&k->refcount, 1u, memory_order_relaxed);
    k->id = atomic_fetch_add_explicit(&g_next_kobj_id, 1u, memory_order_relaxed);
    k->destroy = destroy;
}

void Kobj_Retain(struct Kobj *k)
{
    if (!k) {
        return;
    }
    atomic_fetch_add_explicit(&k->refcount, 1u, memory_order_relaxed);
}

void Kobj_Release(struct Kobj *k)
{
    if (!k) {
        return;
    }
    u32 prev = atomic_fetch_sub_explicit(&k->refcount, 1u, memory_order_acq_rel);
    if (prev == 1u) {
        // We dropped the last reference.
        if (k->destroy) {
            k->destroy(k);
        }
    }
}
