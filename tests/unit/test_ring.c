// SPDX-License-Identifier: BSD-2-Clause
//
// Hosted unit test for cara/ring.h. Exercises init validation, full/empty
// edges, signal-needed semantics, wrap-around, and round-tripping slot
// payloads. Single-threaded; concurrency correctness is by inspection of
// the atomic ordering in the header.

#include <cara/ring.h>
#include <stdio.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_ring: FAIL: %s\n", msg);
    return code;
}

int main(void)
{
    struct RingHeader hdr;

    // Bad capacities are rejected.
    if (Ring_Init(&hdr, 0, 0) != CARA_EINVAL) {
        return fail("init accepted capacity 0", 1);
    }
    if (Ring_Init(&hdr, 3, 0) != CARA_EINVAL) {
        return fail("init accepted non-power-of-two capacity", 2);
    }
    if (Ring_Init(&hdr, 7, 0) != CARA_EINVAL) {
        return fail("init accepted capacity 7", 3);
    }

    enum { CAP = 4 };
    struct RingSlot slots[CAP];

    if (Ring_Init(&hdr, CAP, 0xDEADBEEF) != CARA_EOK) {
        return fail("init failed for valid capacity", 4);
    }
    if (!Ring_IsEmpty(&hdr) || Ring_IsFull(&hdr) || Ring_Count(&hdr) != 0) {
        return fail("fresh ring not empty", 5);
    }
    if (hdr.signal_kobj != 0xDEADBEEF) {
        return fail("signal_kobj not stored", 6);
    }

    // Empty dequeue returns false.
    struct RingSlot out;
    if (Ring_Dequeue(&hdr, slots, &out)) {
        return fail("dequeue from empty returned true", 7);
    }

    // First enqueue signals empty→nonempty.
    bool sig = false;
    struct RingSlot s = { .kind = 1, .length = 8, .payload = 0x1000, .reserved = 0 };
    if (!Ring_Enqueue(&hdr, slots, s, &sig)) {
        return fail("enqueue returned false on empty ring", 8);
    }
    if (!sig) {
        return fail("first enqueue did not flag signal_needed", 9);
    }
    if (Ring_Count(&hdr) != 1 || Ring_IsEmpty(&hdr)) {
        return fail("count wrong after one enqueue", 10);
    }

    // Subsequent enqueues do not signal.
    for (u32 i = 2; i <= CAP; i++) {
        sig = true;
        s.kind = i;
        if (!Ring_Enqueue(&hdr, slots, s, &sig)) {
            return fail("unexpected full while filling", 11);
        }
        if (sig) {
            return fail("signal_needed set on non-edge enqueue", 12);
        }
    }
    if (!Ring_IsFull(&hdr) || Ring_Count(&hdr) != CAP) {
        return fail("ring not full after CAP enqueues", 13);
    }

    // Full ring rejects further enqueue.
    sig = true;
    if (Ring_Enqueue(&hdr, slots, s, &sig)) {
        return fail("enqueue succeeded on full ring", 14);
    }
    if (sig) {
        return fail("signal_needed not cleared on failed enqueue", 15);
    }

    // Dequeue all in FIFO order.
    for (u32 i = 1; i <= CAP; i++) {
        if (!Ring_Dequeue(&hdr, slots, &out)) {
            return fail("dequeue returned false mid-drain", 16);
        }
        if (out.kind != i) {
            return fail("dequeue out-of-order", 17);
        }
        if (out.payload != 0x1000) {
            return fail("payload corrupted across enqueue/dequeue", 18);
        }
    }
    if (!Ring_IsEmpty(&hdr)) {
        return fail("ring not empty after draining", 19);
    }

    // Wrap-around: enqueue+dequeue 4*CAP times through capacity CAP.
    for (u32 i = 0; i < 4 * CAP; i++) {
        sig = false;
        s.kind = 100 + i;
        s.payload = 0x2000 + i;
        if (!Ring_Enqueue(&hdr, slots, s, &sig)) {
            return fail("wrap enqueue failed", 20);
        }
        if (!sig) {
            return fail("wrap empty→nonempty signal lost", 21);
        }

        if (!Ring_Dequeue(&hdr, slots, &out)) {
            return fail("wrap dequeue failed", 22);
        }
        if (out.kind != 100 + i || out.payload != 0x2000 + i) {
            return fail("wrap data corruption", 23);
        }
    }

    if (!Ring_IsEmpty(&hdr)) {
        return fail("post-wrap ring not empty", 24);
    }

    // Cache-line layout sanity. head and tail must be on different lines.
    uintptr_t hbase = (uintptr_t)&hdr.head;
    uintptr_t tbase = (uintptr_t)&hdr.tail;
    if ((hbase / CARA_CACHELINE) == (tbase / CARA_CACHELINE)) {
        return fail("head and tail share a cache line", 25);
    }

    puts("ring smoke ok");
    return 0;
}
