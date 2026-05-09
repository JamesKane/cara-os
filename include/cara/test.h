// SPDX-License-Identifier: BSD-2-Clause
//
// In-kernel test harness. Tests are registered via KERNEL_TEST() into a
// `.kernel_tests` linker section that croi.lds aggregates between
// `__kernel_tests_start` / `__kernel_tests_end`. Test_RunAll walks the
// array on boot, calls each, and prints PASS/FAIL through LOG_*. The
// smoke ctest asserts "kernel tests: N passed, 0 failed".

#ifndef CARA_TEST_H
#define CARA_TEST_H

#include <cara/types.h>

struct TestCtx {
    const char *name;
    bool failed;
    const char *reason; // static string set by TEST_FAIL
};

typedef void (*KernelTestFn)(struct TestCtx *ctx);

struct KernelTestEntry {
    const char *name;
    KernelTestFn fn;
};

#define TEST_FAIL(ctx, msg)                                                                        \
    do {                                                                                           \
        (ctx)->failed = true;                                                                      \
        (ctx)->reason = (msg);                                                                     \
        return;                                                                                    \
    } while (0)

#define TEST_ASSERT(ctx, cond, msg)                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            TEST_FAIL((ctx), (msg));                                                               \
        }                                                                                          \
    } while (0)

#define KERNEL_TEST(NAME)                                                                          \
    static void test_##NAME(struct TestCtx *);                                                     \
    static const struct KernelTestEntry _ktest_##NAME                                              \
        __attribute__((used, section(".kernel_tests"))) = { #NAME, test_##NAME };                  \
    static void test_##NAME(struct TestCtx *ctx)

// Run every registered test. Counts and prints a summary line that
// the QEMU smoke ctest greps for: "kernel tests: N passed, M failed".
void Test_RunAll(void);

#endif
