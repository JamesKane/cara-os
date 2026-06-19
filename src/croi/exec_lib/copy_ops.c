// SPDX-License-Identifier: BSD-2-Clause
//
// exec.library CopyMem / CopyMemQuick (Phase 3 L1; LVO.md §5.1) —
// `local`-flavour LVOs like the list primitives: pure copies on the
// caller's SASOS buffers, run in-process from the shared RX page
// (.lib_text.exec at VA 0x4000_0000). Self-contained — no kernel
// symbols, no globals, no out-of-section calls (so no memcpy()). The
// generated stub appends SysBase as the trailing arg, which we ignore.

#include <exec/types.h>

struct ExecBase;

#define LIBTEXT __attribute__((section(".lib_text.exec"), used))

// CopyMem: byte copy of `size` bytes, source → dest (non-overlapping,
// the V36 contract).
LIBTEXT void Croi_Exec_CopyMem(const APTR source, APTR dest, ULONG size, struct ExecBase *base)
{
    (void)base;
    const unsigned char *s = source;
    unsigned char *d = dest;
    for (ULONG i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

// CopyMemQuick: same result; the V36 contract promises long-aligned
// pointers and a multiple-of-4 size, so copy in 8-byte chunks with a
// byte tail.
LIBTEXT void Croi_Exec_CopyMemQuick(const APTR source, APTR dest, ULONG size, struct ExecBase *base)
{
    (void)base;
    const unsigned long long *s = source;
    unsigned long long *d = dest;
    ULONG words = size / 8;
    for (ULONG i = 0; i < words; i++) {
        d[i] = s[i];
    }
    const unsigned char *sb = (const unsigned char *)(s + words);
    unsigned char *db = (unsigned char *)(d + words);
    for (ULONG i = 0; i < (size & 7u); i++) {
        db[i] = sb[i];
    }
}
