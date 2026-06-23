/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * <sys/types.h> — CaraOS userland libc (Phase T). The handful of POSIX
 * typedefs a ported tool reaches for. docs/PORTS.md §1.1.
 */

#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H

#include <stddef.h>

typedef long time_t;
typedef long clock_t;
typedef long ssize_t;
typedef long off_t;

#endif /* _SYS_TYPES_H */
