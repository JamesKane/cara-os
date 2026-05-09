// SPDX-License-Identifier: BSD-2-Clause
//
// Cara attribute macros. C23 attributes plus a couple of GNU-specific spellings
// kept behind a stable name so the rest of the codebase doesn't have to care
// which dialect the compiler accepts. Clang implements the C23 spellings; we
// only fall back to vendor attributes for things C23 doesn't standardise.

#ifndef CARA_ATTR_H
#define CARA_ATTR_H

#define CARA_NORETURN [[noreturn]]
#define CARA_UNUSED [[maybe_unused]]
#define CARA_NODISCARD [[nodiscard]]

#define CARA_PACKED [[gnu::packed]]
#define CARA_ALIGNED(N) [[gnu::aligned(N)]]
#define CARA_USED [[gnu::used]]
#define CARA_NAKED [[gnu::naked]]
#define CARA_SECTION(S) [[gnu::section(S)]]

constexpr unsigned int CARA_CACHELINE = 64;

#endif
