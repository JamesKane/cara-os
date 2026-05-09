// SPDX-License-Identifier: BSD-2-Clause
//
// Cara kernel objects (Kobj) and per-task handle tables.
//
// Every kernel resource user space can hold a reference to (Task,
// MsgPort, Library, Signal kobj, Ring, MemRegion, Device, Volume,
// Lock) embeds a struct Kobj as its first field. Refcount + a
// type-specific destroy callback give a uniform lifecycle.
//
// User space — and other kernel modules that take refs across tasks
// — go through Handles rather than raw Kobj pointers. A Handle is a
// 32-bit value of (generation << 16) | slot_index, validated on every
// lookup against (slot in range, target non-null, generation matches,
// type matches). Stale handles never fault; they return CARA_EBADF.

#ifndef CARA_KOBJ_H
#define CARA_KOBJ_H

#include <cara/types.h>

#include <stdatomic.h>

typedef enum : u16 {
    KOBJ_NONE = 0,
    KOBJ_TASK = 1,
    KOBJ_MSGPORT = 2,
    KOBJ_LIBRARY = 3,
    KOBJ_SIGNAL = 4,
    KOBJ_RING = 5,
    KOBJ_MEMREGION = 6,
    KOBJ_DEVICE = 7,
    KOBJ_VOLUME = 8,
    KOBJ_LOCK = 9,
} KobjType;

struct Kobj {
    KobjType type;
    u16 flags;
    _Atomic u32 refcount;
    u64 id;
    void (*destroy)(struct Kobj *); // may be nullptr (no-op release)
};

// Initialise a Kobj. Refcount starts at 1 (caller's ref). destroy may
// be nullptr — in that case Kobj_Release just decrements; caller owns
// the storage.
void Kobj_Init(struct Kobj *k, KobjType type, void (*destroy)(struct Kobj *));

void Kobj_Retain(struct Kobj *k);
void Kobj_Release(struct Kobj *k); // calls destroy when refcount reaches 0

// ---- Handle table -------------------------------------------------------

typedef u32 Handle;
constexpr Handle HANDLE_INVALID = 0xFFFFFFFFu;

struct HandleSlot {
    struct Kobj *target; // nullptr ⇒ slot is free
    u16 generation;      // bumped on close
    u16 _pad;
    u32 next_free; // index of next free slot, or HANDLE_INVALID
};

struct HandleTable {
    struct HandleSlot *slots;
    u32 cap;
    u32 free_head; // HANDLE_INVALID = no free slots
};

// Initialise a HandleTable with `cap` slots (allocated from the kernel
// heap). cap must be ≤ 65535 (slot index field is 16 bits).
[[nodiscard]] int HandleTable_Init(struct HandleTable *ht, u32 cap);

void HandleTable_Destroy(struct HandleTable *ht);

// Open a new handle to `target`. Bumps target's refcount on success.
[[nodiscard]] int HandleTable_Open(struct HandleTable *ht, struct Kobj *target, Handle *out);

// Look up a handle. Returns CARA_EOK and writes *out on success. Returns
// CARA_EBADF on any of: bad slot index, freed slot, generation mismatch,
// type mismatch (when expected != KOBJ_NONE).
[[nodiscard]] int HandleTable_Lookup(const struct HandleTable *ht, Handle h, KobjType expected,
                                     struct Kobj **out);

// Close a handle. Releases the underlying Kobj's refcount and bumps
// the slot's generation so the same Handle value cannot be re-used.
[[nodiscard]] int HandleTable_Close(struct HandleTable *ht, Handle h);

#endif
