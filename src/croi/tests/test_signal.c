// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(signal_smoke): producer/consumer ping-pong via signals.
// The consumer task calls Croi_AllocSignal once at start; the producer
// task signals that bit five times via Croi_Signal; the consumer
// observes each via Croi_Wait. Test asserts every signal arrived
// exactly once and the consumer mask carried no spurious bits.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/test.h>
#include <cara/types.h>

static struct Task *g_consumer;
static i32 g_sig_bit;
static u32 g_received;
static bool g_failed;

static void consumer(void *arg)
{
    (void)arg;
    g_sig_bit = Croi_AllocSignal();
    if (g_sig_bit < 0) {
        g_failed = true;
        return;
    }
    while (g_received < 5) {
        u32 hit = Croi_Wait(1u << (u32)g_sig_bit);
        if (hit != (1u << (u32)g_sig_bit)) {
            g_failed = true;
            return;
        }
        g_received++;
    }
    Croi_FreeSignal(g_sig_bit);
}

static void producer(void *arg)
{
    (void)arg;
    // Wait until consumer has registered its bit.
    while (g_sig_bit < 0 && !g_failed) {
        Croi_Yield();
    }
    if (g_failed) {
        return;
    }
    for (u32 i = 0; i < 5; i++) {
        Croi_Yield();
        Croi_Signal(g_consumer, 1u << (u32)g_sig_bit);
    }
}

KERNEL_TEST(signal_smoke)
{
    g_received = 0;
    g_failed = false;
    g_sig_bit = -1;
    g_consumer = nullptr;

    g_consumer = Croi_SpawnKernelTask("cons", 5, consumer, nullptr);
    TEST_ASSERT(ctx, g_consumer != nullptr, "spawn consumer failed");
    TEST_ASSERT(ctx, Croi_SpawnKernelTask("prod", 5, producer, nullptr) != nullptr,
                "spawn producer failed");

    Croi_TaskSetSelfPriority(-1);
    while (g_received < 5 && !g_failed) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    TEST_ASSERT(ctx, !g_failed, "consumer reported a failure");
    TEST_ASSERT(ctx, g_received == 5, "received count != 5");
}
