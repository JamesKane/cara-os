// SPDX-License-Identifier: BSD-2-Clause
//
// Self-test runner. Walks the .kernel_tests linker section between
// __kernel_tests_start and __kernel_tests_end, executes each entry,
// and emits a PASS/FAIL line plus an aggregate summary.

#include <cara/log.h>
#include <cara/sched.h>
#include <cara/test.h>
#include <cara/types.h>

extern const struct KernelTestEntry __kernel_tests_start[];
extern const struct KernelTestEntry __kernel_tests_end[];

void Test_RunAll(void)
{
    u32 passed = 0;
    u32 failed = 0;
    const struct KernelTestEntry *e = __kernel_tests_start;
    const struct KernelTestEntry *end = __kernel_tests_end;

    LOG_INFO("test", "running %llu kernel tests", (u64)(end - e));

    for (; e < end; e++) {
        struct TestCtx ctx = {
            .name = e->name,
            .failed = false,
            .reason = "",
        };
        e->fn(&ctx);
        if (ctx.failed) {
            LOG_ERROR("test", "%s ... FAIL: %s", e->name, ctx.reason);
            failed++;
        } else {
            LOG_INFO("test", "%s ... PASS", e->name);
            passed++;
        }
        // Reap any tasks the test spawned and exited so the heap
        // doesn't accumulate state across the suite.
        Sched_ReapDead();
    }

    LOG_INFO("test", "kernel tests: %u passed, %u failed", passed, failed);
}
