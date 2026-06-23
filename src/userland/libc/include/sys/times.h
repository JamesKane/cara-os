/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * <sys/times.h> — CaraOS userland libc (Phase T). struct tms + times(),
 * backed by the monotonic SYS_CurrentTime clock at HZ=100 (centiseconds)
 * — the granularity old benchmarks (Dhrystone) assume. docs/PORTS.md §1.1.
 */

#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

#include <sys/types.h>

struct tms {
    clock_t tms_utime;
    clock_t tms_stime;
    clock_t tms_cutime;
    clock_t tms_cstime;
};

clock_t times(struct tms *buf);

#endif /* _SYS_TIMES_H */
