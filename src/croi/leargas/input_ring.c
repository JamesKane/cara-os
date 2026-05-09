// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas L0 — input event ring. SPSC contract: one producer task
// (Phase 1: in-kernel HID poll path; Phase 3: U-mode HID Gleas) and
// one consumer task (Phase 1: Leargas pointer/window router; Phase 3:
// input.device → intuition.library). The ring is a fixed-capacity
// in-process singleton; per-window IDCMP MsgPorts (LF.1) build on
// `cara/ring.h` separately.
//
// Why not ride `cara/ring.h` directly: that ring is byte-symmetric
// across `struct RingSlot` and is the primitive every CroiMsgPort
// builds on. Input events are a typed event stream — no reply port,
// no node, fixed payload — so the value of going through the
// generic primitive is the 30 lines of atomics, which we'd rather
// have type-checked against `struct LeargasInputEvent` directly.
//
// Memory ordering matches the SPSC pattern in `cara/ring.h`:
// producer relaxed-load(head) + acquire-load(tail) + release-store(head);
// consumer relaxed-load(tail) + acquire-load(head) + release-store(tail).

#include <cara/leargas.h>
#include <cara/types.h>

#include <stdatomic.h>

static_assert((LEARGAS_INPUT_RING_CAP & (LEARGAS_INPUT_RING_CAP - 1)) == 0,
              "LEARGAS_INPUT_RING_CAP must be a power of two");

static struct {
    struct LeargasInputEvent slots[LEARGAS_INPUT_RING_CAP];
    _Atomic u32 head; // producer writes
    _Atomic u32 tail; // consumer writes
    bool initialised;
} g_input_ring;

[[nodiscard]] int Leargas_Input_Init(void)
{
    // Idempotent: re-Init while running just observes the existing
    // ring. The head/tail load is plain because pre-Init the ring
    // is uninitialised storage; on subsequent calls the values are
    // whatever the running producer/consumer have driven them to,
    // and we leave them alone.
    if (g_input_ring.initialised) {
        return CARA_EOK;
    }
    atomic_store_explicit(&g_input_ring.head, 0, memory_order_relaxed);
    atomic_store_explicit(&g_input_ring.tail, 0, memory_order_relaxed);
    g_input_ring.initialised = true;
    return CARA_EOK;
}

[[nodiscard]] bool Leargas_Input_Post(const struct LeargasInputEvent *ev)
{
    if (!ev || !g_input_ring.initialised) {
        return false;
    }
    u32 head = atomic_load_explicit(&g_input_ring.head, memory_order_relaxed);
    u32 tail = atomic_load_explicit(&g_input_ring.tail, memory_order_acquire);

    if ((head - tail) >= LEARGAS_INPUT_RING_CAP) {
        return false;
    }

    g_input_ring.slots[head & (LEARGAS_INPUT_RING_CAP - 1)] = *ev;
    atomic_store_explicit(&g_input_ring.head, head + 1, memory_order_release);
    return true;
}

[[nodiscard]] bool Leargas_Input_Read(struct LeargasInputEvent *out)
{
    if (!out || !g_input_ring.initialised) {
        return false;
    }
    u32 tail = atomic_load_explicit(&g_input_ring.tail, memory_order_relaxed);
    u32 head = atomic_load_explicit(&g_input_ring.head, memory_order_acquire);

    if (head == tail) {
        return false;
    }

    *out = g_input_ring.slots[tail & (LEARGAS_INPUT_RING_CAP - 1)];
    atomic_store_explicit(&g_input_ring.tail, tail + 1, memory_order_release);
    return true;
}

[[nodiscard]] u32 Leargas_Input_Pending(void)
{
    if (!g_input_ring.initialised) {
        return 0;
    }
    u32 head = atomic_load_explicit(&g_input_ring.head, memory_order_acquire);
    u32 tail = atomic_load_explicit(&g_input_ring.tail, memory_order_acquire);
    return head - tail;
}

void Leargas_Input_Reset(void)
{
    atomic_store_explicit(&g_input_ring.head, 0, memory_order_relaxed);
    atomic_store_explicit(&g_input_ring.tail, 0, memory_order_relaxed);
    g_input_ring.initialised = true;
}
