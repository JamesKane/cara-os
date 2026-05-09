// SPDX-License-Identifier: BSD-2-Clause
//
// Sstc deadline-timer test. Schedules a 100 ms one-shot, waits in WFI
// until the trap dispatcher acknowledges via Croi_Time_DeadlineFired,
// and asserts elapsed >= target. The upper bound is generous (2× the
// target) because virtualised hosts add scheduler-quantum jitter that
// real silicon would not — the property we're really testing is "Sstc
// fires, the trap path delivers it, the foreground sees the flag."

#include <cara/test.h>
#include <cara/time.h>
#include <cara/types.h>

KERNEL_TEST(time_smoke)
{
    const u64 target_ns = 100ull * 1000ull * 1000ull; // 100 ms

    u64 start = Croi_Time_Now();
    Croi_Time_SetDeadline(start + target_ns);
    while (!Croi_Time_DeadlineFired()) {
        __asm__ volatile("wfi");
    }
    u64 elapsed = Croi_Time_Now() - start;

    TEST_ASSERT(ctx, elapsed >= target_ns, "deadline fired too early");

    // 2× target accommodates QEMU host-scheduler quantum (~30-100 ms).
    TEST_ASSERT(ctx, elapsed <= 2 * target_ns, "deadline fired more than 2x late");
}
