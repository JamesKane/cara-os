// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas — the brand-namespace Phase 1 minimum window-system
// substrate. Phase 3's `intuition.library` LVO surface trampolines
// into the same `Leargas_*` symbols this header declares (see
// docs/PHASE1_LEARGAS.md and ARCHITECTURE.md §11).
//
// L0 — Input event ring. The seam between the (Phase 1: kernel HID
// poll, Phase 3: HID Gleas) producer side and the (Phase 1: pointer
// + window router, Phase 3: input.device → intuition) consumer side.
// SPSC by contract: exactly one producer task and one consumer task
// at a time. Phase 1 has both running in-kernel; Phase 3 moves the
// producer to a U-mode HID Gleas without changing the contract.

#ifndef CARA_LEARGAS_H
#define CARA_LEARGAS_H

#include <cara/types.h>

// ---- Flat input event record ----------------------------------------------
//
// Brand-namespace flat shape that flows through the Phase 1 input
// ring. Distinct from V36+ `struct InputEvent` (which is a linked-list
// node with an embedded `struct timeval`); when Phase 3 publishes
// input.device the LVO body translates between the two.
//
// Field semantics, by ie_class:
//
//   IECLASS_RAWKEY:   ie_code = V36+ rawkey number (bit 7 = up-stroke
//                     via IECODE_UP_PREFIX). ie_qualifier = IEQUALIFIER_*
//                     modifier bitmap. ie_dx/dy unused.
//   IECLASS_RAWMOUSE: ie_code = IECODE_LBUTTON / RBUTTON / MBUTTON
//                     for transitions, IECODE_NOBUTTON for motion-only.
//                     ie_qualifier = IEQUALIFIER_RELATIVEMOUSE plus
//                     held-button bits. ie_dx/dy = signed pixel deltas.
//
// Sized so that producers can construct one on the stack with a
// trivial sequence of stores; the impl copies it byte-for-byte into
// the ring slot.

struct LeargasInputEvent {
    u8 ie_class;     // IECLASS_*
    u8 ie_subclass;  // IESUBCLASS_* (default IESUBCLASS_COMPATIBLE = 0)
    u16 ie_code;     // IECODE_* / rawkey
    u16 ie_qualifier;// IEQUALIFIER_*
    u16 _ie_pad0;    // explicit pad — keeps the layout deterministic
    i16 ie_dx;       // RAWMOUSE: X delta (pixels)
    i16 ie_dy;       // RAWMOUSE: Y delta (pixels)
    u32 _ie_pad1;    // explicit pad — see above
    u64 ie_ts_ns;    // boot-time nanoseconds; producer fills from Croi_Time_Now
};
static_assert(sizeof(struct LeargasInputEvent) == 24,
              "LeargasInputEvent layout drifted; ring slot pun depends on it");

// ---- Phase 1 input-ring API ------------------------------------------------
//
// The ring is a process-wide singleton in Phase 1: there is exactly
// one input event stream and one consumer (the Leargas window-event
// router). Initialised once at boot; subsequent Init calls are
// idempotent and return CARA_EOK.

constexpr u32 LEARGAS_INPUT_RING_CAP = 64;

// Initialise the input ring. Idempotent. Capacity is fixed at
// LEARGAS_INPUT_RING_CAP (64) — boot-protocol HID at typical poll
// rates (kbd ≤ 8 keys, mouse ≤ 200 Hz) leaves >>40ms of headroom
// before backpressure, which is far longer than Leargas's drain
// cadence in any sensible Phase 1 configuration.
[[nodiscard]] int Leargas_Input_Init(void);

// Producer side. Returns true on success, false when the ring is
// full (caller drops the event). Wait-free; safe from a HID interrupt
// path. ev is read-only and copied; caller may free immediately.
[[nodiscard]] bool Leargas_Input_Post(const struct LeargasInputEvent *ev);

// Consumer side. Returns true on success, false when the ring is
// empty. Wait-free. Phase 1 callers poll; signal-based wake of a
// blocked consumer task arrives with LA when the pointer Gleas
// blocks on the ring. *out is written exactly when the call returns
// true.
[[nodiscard]] bool Leargas_Input_Read(struct LeargasInputEvent *out);

// Number of events queued. Lock-free snapshot — value is correct as
// of the moment of the call but may change before the caller reads
// it; Phase 1 uses this only for instrumentation and tests.
[[nodiscard]] u32 Leargas_Input_Pending(void);

// Reset the ring to empty. Test-only; production callers (HID
// producer, Leargas consumer) never invoke this. Safe to call
// before Init has run.
void Leargas_Input_Reset(void);

#endif // CARA_LEARGAS_H
