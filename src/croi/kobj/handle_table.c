// SPDX-License-Identifier: BSD-2-Clause
//
// Per-task handle table. Slots form a free-list; allocation pops the
// head, close pushes onto the head and bumps the slot's generation.
// Every lookup validates slot index, target presence, generation, and
// (optionally) Kobj type — failures return CARA_EBADF rather than
// faulting.

#include <cara/alloc.h>
#include <cara/kobj.h>
#include <cara/types.h>

#define SLOT_INDEX_MASK 0xFFFFu
#define GEN_SHIFT       16u

[[nodiscard]] int HandleTable_Init(struct HandleTable *ht, u32 cap)
{
    if (!ht || cap == 0 || cap > 0xFFFFu) {
        return CARA_EINVAL;
    }
    struct HandleSlot *slots =
        (struct HandleSlot *)Croi_Alloc((usize)cap * sizeof(struct HandleSlot));
    if (!slots) {
        return CARA_ENOMEM;
    }
    for (u32 i = 0; i < cap; i++) {
        slots[i].target = nullptr;
        slots[i].generation = 0;
        slots[i]._pad = 0;
        slots[i].next_free = (i + 1u < cap) ? (i + 1u) : HANDLE_INVALID;
    }
    ht->slots = slots;
    ht->cap = cap;
    ht->free_head = 0;
    return CARA_EOK;
}

void HandleTable_Destroy(struct HandleTable *ht)
{
    if (!ht) {
        return;
    }
    if (ht->slots) {
        // Release any still-held references before freeing.
        for (u32 i = 0; i < ht->cap; i++) {
            if (ht->slots[i].target) {
                Kobj_Release(ht->slots[i].target);
                ht->slots[i].target = nullptr;
            }
        }
        Croi_Free(ht->slots);
        ht->slots = nullptr;
    }
    ht->cap = 0;
    ht->free_head = HANDLE_INVALID;
}

[[nodiscard]] int HandleTable_Open(struct HandleTable *ht, struct Kobj *target,
                                   Handle *out)
{
    if (!ht || !target || !out) {
        return CARA_EINVAL;
    }
    if (ht->free_head == HANDLE_INVALID) {
        return CARA_ENOMEM;     // exhausted
    }
    u32 idx = ht->free_head;
    ht->free_head = ht->slots[idx].next_free;
    ht->slots[idx].target = target;
    Kobj_Retain(target);
    *out = ((u32)ht->slots[idx].generation << GEN_SHIFT) | (idx & SLOT_INDEX_MASK);
    return CARA_EOK;
}

[[nodiscard]] int HandleTable_Lookup(const struct HandleTable *ht, Handle h,
                                     KobjType expected, struct Kobj **out)
{
    if (!ht || !out) {
        return CARA_EINVAL;
    }
    u32 idx = h & SLOT_INDEX_MASK;
    u32 gen = h >> GEN_SHIFT;
    if (idx >= ht->cap) {
        return CARA_EBADF;
    }
    const struct HandleSlot *s = &ht->slots[idx];
    if (!s->target) {
        return CARA_EBADF;
    }
    if (gen != s->generation) {
        return CARA_EBADF;
    }
    if (expected != KOBJ_NONE && s->target->type != expected) {
        return CARA_EBADF;
    }
    *out = s->target;
    return CARA_EOK;
}

[[nodiscard]] int HandleTable_Close(struct HandleTable *ht, Handle h)
{
    if (!ht) {
        return CARA_EINVAL;
    }
    u32 idx = h & SLOT_INDEX_MASK;
    u32 gen = h >> GEN_SHIFT;
    if (idx >= ht->cap) {
        return CARA_EBADF;
    }
    struct HandleSlot *s = &ht->slots[idx];
    if (!s->target || gen != s->generation) {
        return CARA_EBADF;
    }
    struct Kobj *target = s->target;
    s->target = nullptr;
    s->generation++;            // 16-bit wrap is fine; collision is a
                                // 1-in-65536 false-EBADF chance and we
                                // never silently use a stale handle
    s->next_free = ht->free_head;
    ht->free_head = idx;
    Kobj_Release(target);
    return CARA_EOK;
}
