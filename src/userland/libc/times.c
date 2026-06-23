// SPDX-License-Identifier: BSD-2-Clause
//
// CaraOS userland libc — times() (Phase T). Backed by SYS_CurrentTime
// (monotonic seconds + microseconds since boot), reported as centiseconds
// so tms_utime matches the HZ=100 convention old benchmarks assume.
// docs/PORTS.md §1.1.

#include <cara/sysno.h>
#include <sys/times.h>

clock_t times(struct tms *buf)
{
    // SYS_CurrentTime(ULONG *seconds, ULONG *micros) — 32-bit out params.
    unsigned int s = 0, us = 0;
    register long a0 __asm__("a0") = (long)(unsigned long)&s;
    register long a1 __asm__("a1") = (long)(unsigned long)&us;
    register long a7 __asm__("a7") = SYS_CurrentTime;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a7) : "memory");

    clock_t t = (clock_t)s * 100 + (clock_t)(us / 10000u); // centiseconds
    if (buf) {
        buf->tms_utime = t;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return t;
}
