// SPDX-License-Identifier: BSD-2-Clause
//
// SBI (Supervisor Binary Interface) helpers. Per the RISC-V SBI spec v2.0.
// We use only what early Croi needs: probe an extension, push a byte to
// the debug console (DBCN, eid 'DBCN'), or fall back to the legacy putchar
// (eid 0x01) when the firmware is older.

#ifndef CARA_CROI_SBI_H
#define CARA_CROI_SBI_H

#include <cara/types.h>

struct SbiRet {
    i64 error;
    i64 value;
};

// Generic ecall. Returns (a0, a1) packed into SbiRet (a0 = error, a1 = value).
struct SbiRet sbi_ecall(u64 eid, u64 fid, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4,
                        u64 a5);

// SBI base extension (eid 0x10).
#define SBI_EID_BASE 0x10
enum {
    SBI_BASE_FID_GET_SPEC_VERSION   = 0,
    SBI_BASE_FID_GET_IMPL_ID        = 1,
    SBI_BASE_FID_GET_IMPL_VERSION   = 2,
    SBI_BASE_FID_PROBE_EXTENSION    = 3,
};

// Returns true if SBI advertises the given extension id.
bool sbi_probe_extension(u64 eid);

// Legacy console putchar (eid 0x01). Only call if probe says DBCN is missing.
void sbi_legacy_putchar(char c);

// Debug Console (DBCN, eid 0x4442434E).
#define SBI_EID_DBCN 0x4442434EUL
enum {
    SBI_DBCN_FID_WRITE      = 0,
    SBI_DBCN_FID_READ       = 1,
    SBI_DBCN_FID_WRITE_BYTE = 2,
};

// Push one byte to the SBI debug console. Falls back automatically to
// the legacy putchar when DBCN is unavailable.
void sbi_putc(char c);

void sbi_puts(const char *s);

#endif
