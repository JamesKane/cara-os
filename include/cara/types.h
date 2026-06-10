// SPDX-License-Identifier: BSD-2-Clause
//
// Cara fixed-width types and common error codes. The kernel and userspace both
// include this in preference to <stdint.h> directly so freestanding builds
// (Croi, Splanc) don't pull in hosted-only headers.

#ifndef CARA_TYPES_H
#define CARA_TYPES_H

#include <cara/attr.h>

#if __has_include(<stdint.h>)
#include <stddef.h>
#include <stdint.h>
#else
// Freestanding: define the fixed-width and size types ourselves. C23
// makes `bool`, `true`, `false`, and `nullptr` built-in keywords, so we
// don't redeclare them.
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned long uint64_t;
typedef signed long int64_t;
typedef unsigned long uintptr_t;
typedef signed long intptr_t;
typedef unsigned long size_t;
typedef signed long ptrdiff_t;
#ifndef NULL
#define NULL nullptr
#endif
#ifndef offsetof
#define offsetof(T, M) __builtin_offsetof(T, M)
#endif
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t usize;
typedef intptr_t iptr;
typedef uintptr_t uptr;

// Common Cara error codes. APIs return 0 on success and a negative error code
// on failure; callers can compare directly against CARA_E* without bitmask
// games. Keep these contiguous and stable.
enum {
    CARA_EOK = 0,
    CARA_EINVAL = -1,
    CARA_ENOENT = -2,
    CARA_ERANGE = -3,
    CARA_EBADF = -4,
    CARA_EAGAIN = -5,
    CARA_ENOMEM = -6,
    CARA_EBADMAGIC = -7,
    CARA_EBADVERSION = -8,
    CARA_EOVERFLOW = -9,
    CARA_ENOTFOUND = -10,
    CARA_EIO = -11,
};

CARA_NORETURN void Croi_Halt(void);

#endif
