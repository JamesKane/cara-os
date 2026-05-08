// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(sched_smoke): cooperative scheduling correctness.
// Spawn 2 high-priority workers (pri 10) and 2 low-priority workers
// (pri 5); each worker logs an iteration, yields, repeats N times,
// then exits. The test (running as kmain) drops to pri -1 so it
// won't be picked over any worker, then yields in a loop until all
// 4 workers have completed. Asserts every worker ran the expected
// number of iterations and that the high-pri pair finished before
// the low-pri pair started running.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/test.h>
#include <cara/types.h>

#define ITERS 5
#define WORKERS 4

static u32 g_done;
static u32 g_iters[WORKERS];
static u32 g_first_low_iter_at_done_high;

struct WorkerArg {
    u32 idx;
    bool low_pri;
};

static struct WorkerArg g_args[WORKERS];

static void worker(void *arg)
{
    struct WorkerArg *wa = (struct WorkerArg *)arg;
    for (u32 i = 0; i < ITERS; i++) {
        // Snapshot ordering signal: when a low-pri worker logs its
        // first iteration, record how many high-pri completions have
        // happened so far. We expect this snapshot to equal 2 (both
        // high-pri workers fully done before any low-pri runs once).
        if (wa->low_pri && g_iters[wa->idx] == 0) {
            g_first_low_iter_at_done_high = g_done;
        }
        g_iters[wa->idx]++;
        Croi_Yield();
    }
    g_done++;
}

KERNEL_TEST(sched_smoke)
{
    // Reset counters in case the harness ever re-runs.
    g_done = 0;
    g_first_low_iter_at_done_high = 999;
    for (u32 i = 0; i < WORKERS; i++) {
        g_iters[i] = 0;
    }

    g_args[0] = (struct WorkerArg){ .idx = 0, .low_pri = false };
    g_args[1] = (struct WorkerArg){ .idx = 1, .low_pri = false };
    g_args[2] = (struct WorkerArg){ .idx = 2, .low_pri = true };
    g_args[3] = (struct WorkerArg){ .idx = 3, .low_pri = true };

    TEST_ASSERT(ctx,
                Croi_SpawnKernelTask("A", 10, worker, &g_args[0]) != nullptr,
                "spawn A failed");
    TEST_ASSERT(ctx,
                Croi_SpawnKernelTask("B", 10, worker, &g_args[1]) != nullptr,
                "spawn B failed");
    TEST_ASSERT(ctx,
                Croi_SpawnKernelTask("C", 5,  worker, &g_args[2]) != nullptr,
                "spawn C failed");
    TEST_ASSERT(ctx,
                Croi_SpawnKernelTask("D", 5,  worker, &g_args[3]) != nullptr,
                "spawn D failed");

    // Drop kmain below all workers so they get to run.
    Croi_TaskSetSelfPriority(-1);
    while (g_done < WORKERS) {
        Croi_Yield();
    }
    Croi_TaskSetSelfPriority(100);

    for (u32 i = 0; i < WORKERS; i++) {
        TEST_ASSERT(ctx, g_iters[i] == ITERS, "worker iter count wrong");
    }

    // The first iteration of any low-pri worker must happen only
    // *after* both high-pri workers have completed all ITERS rounds
    // and exited (g_done was 2 at that moment).
    TEST_ASSERT(ctx, g_first_low_iter_at_done_high == 2,
                "low-pri ran before high-pri finished");
}
