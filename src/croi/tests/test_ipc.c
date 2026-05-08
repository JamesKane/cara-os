// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(ipc_smoke): four producer tasks plus one receiver moving
// 20 messages through a single MsgPort. The receiver allocates a
// signal bit, creates the port over its own task, drains via
// Croi_WaitPort + Croi_GetMsg until the expected total arrives.
// Asserts no message lost and per-producer counts add up to the total.

#include <cara/log.h>
#include <cara/msgport.h>
#include <cara/sched.h>
#include <cara/test.h>
#include <cara/types.h>

#define PRODUCERS 4
#define MSGS_PER_PRODUCER 5
#define TOTAL_MSGS (PRODUCERS * MSGS_PER_PRODUCER)
#define RING_CAP 8                          // smaller than PRODUCERS*MSGS to
                                            // exercise the full path

static struct MsgPort *g_port;
static struct Task    *g_receiver;
static u32             g_received;
static u32             g_per_producer[PRODUCERS];
static bool            g_failed;

static void receiver(void *arg)
{
    (void)arg;
    struct Task *me = Sched_Current();
    i32 sig = Croi_AllocSignal();
    if (sig < 0) {
        g_failed = true;
        return;
    }
    g_port = Croi_CreateMsgPort(me, (u32)sig, RING_CAP);
    if (!g_port) {
        g_failed = true;
        return;
    }

    while (g_received < TOTAL_MSGS) {
        Croi_WaitPort(g_port);
        struct RingSlot msg;
        while (Croi_GetMsg(g_port, &msg)) {
            u32 producer_idx = (u32)(msg.payload >> 16);
            if (producer_idx < PRODUCERS) {
                g_per_producer[producer_idx]++;
            }
            g_received++;
        }
    }
    Croi_DestroyMsgPort(g_port);
    g_port = nullptr;
    Croi_FreeSignal(sig);
}

static void producer(void *arg)
{
    u32 idx = (u32)(uptr)arg;
    while (g_port == nullptr && !g_failed) {
        Croi_Yield();
    }
    if (g_failed) {
        return;
    }
    for (u32 i = 0; i < MSGS_PER_PRODUCER; i++) {
        struct RingSlot msg = {
            .kind = 1,
            .length = 4,
            .payload = ((uptr)idx << 16) | (uptr)i,
            .reserved = 0,
        };
        // Retry on full ring — yield to let the receiver drain.
        while (!Croi_PutMsg(g_port, msg)) {
            Croi_Yield();
        }
    }
}

KERNEL_TEST(ipc_smoke)
{
    g_port = nullptr;
    g_receiver = nullptr;
    g_received = 0;
    g_failed = false;
    for (u32 i = 0; i < PRODUCERS; i++) {
        g_per_producer[i] = 0;
    }

    g_receiver = Croi_SpawnKernelTask("recv", 5, receiver, nullptr);
    TEST_ASSERT(ctx, g_receiver != nullptr, "spawn receiver failed");

    static const char *names[PRODUCERS] = { "prod0", "prod1", "prod2", "prod3" };
    for (u32 i = 0; i < PRODUCERS; i++) {
        TEST_ASSERT(ctx,
                    Croi_SpawnKernelTask(names[i], 5, producer,
                                         (void *)(uptr)i)
                        != nullptr,
                    "spawn producer failed");
    }

    Croi_TaskSetSelfPriority(-1);
    while (g_received < TOTAL_MSGS && !g_failed) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    TEST_ASSERT(ctx, !g_failed, "receiver/port failure");
    TEST_ASSERT(ctx, g_received == TOTAL_MSGS, "received != total");
    for (u32 i = 0; i < PRODUCERS; i++) {
        TEST_ASSERT(ctx, g_per_producer[i] == MSGS_PER_PRODUCER,
                    "per-producer count wrong");
    }
}
