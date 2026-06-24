// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(console_input): the T.3.1 cooked console-input line discipline
// (docs/PORTS.md §6) driven via Croi_ConsoleInput_Inject (the test/UART
// feed). Each case injects a COMPLETE line (with a terminator) so the
// blocking read returns without yielding. Proves: line delivery,
// backspace editing, and short-read draining across calls.

#include <cara/console_input.h>
#include <cara/test.h>
#include <cara/types.h>

KERNEL_TEST(console_input)
{
    char buf[32];

    // A plain line is delivered with a trailing '\n' (the '\r'/'\n' the
    // user types is normalised to one '\n').
    Croi_ConsoleInput_Inject("hi\n", 3);
    usize n = Croi_ConsoleInput_Read(buf, sizeof buf);
    TEST_ASSERT(ctx, n == 3 && buf[0] == 'h' && buf[1] == 'i' && buf[2] == '\n', "plain line");

    // Backspace (0x08) edits the line before Enter: "ab\b\bxy" -> "xy".
    Croi_ConsoleInput_Inject("ab\x08\x08xy\n", 7);
    n = Croi_ConsoleInput_Read(buf, sizeof buf);
    TEST_ASSERT(ctx, n == 3 && buf[0] == 'x' && buf[1] == 'y' && buf[2] == '\n', "backspace edit");

    // A short read returns part of the line; the rest comes on the next
    // read (no new input consumed).
    Croi_ConsoleInput_Inject("abc\n", 4);
    n = Croi_ConsoleInput_Read(buf, 2);
    TEST_ASSERT(ctx, n == 2 && buf[0] == 'a' && buf[1] == 'b', "short read part 1");
    n = Croi_ConsoleInput_Read(buf, sizeof buf);
    TEST_ASSERT(ctx, n == 2 && buf[0] == 'c' && buf[1] == '\n', "short read part 2");
}
