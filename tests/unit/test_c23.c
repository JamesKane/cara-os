// SPDX-License-Identifier: BSD-2-Clause
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

[[nodiscard]] static int answer(void)
{
    return 42;
}

constexpr int CARA_TEST_CONSTEXPR = 7;

struct Point {
    int x;
    int y;
};

int main(void)
{
    int *p = nullptr;
    if (p != nullptr) {
        return 1;
    }

    int a = 100;
    typeof(a) b = a;
    if (b != 100) {
        return 2;
    }

    if (answer() != 42) {
        return 3;
    }

    if (CARA_TEST_CONSTEXPR != 7) {
        return 4;
    }

    _Atomic uint32_t counter = 0;
    atomic_store_explicit(&counter, 1, memory_order_release);
    if (atomic_load_explicit(&counter, memory_order_acquire) != 1) {
        return 5;
    }

    struct Point pt = { .x = 3, .y = 4 };
    if (pt.x + pt.y != 7) {
        return 6;
    }

    puts("c23 smoke ok");
    return 0;
}
