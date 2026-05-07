// SPDX-License-Identifier: BSD-2-Clause

#include "sbi.h"

#include <cara/types.h>

struct SbiRet sbi_ecall(u64 eid, u64 fid, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4,
                        u64 a5)
{
    register u64 _a7 __asm__("a7") = eid;
    register u64 _a6 __asm__("a6") = fid;
    register u64 _a0 __asm__("a0") = a0;
    register u64 _a1 __asm__("a1") = a1;
    register u64 _a2 __asm__("a2") = a2;
    register u64 _a3 __asm__("a3") = a3;
    register u64 _a4 __asm__("a4") = a4;
    register u64 _a5 __asm__("a5") = a5;
    __asm__ volatile("ecall"
                     : "+r"(_a0), "+r"(_a1)
                     : "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5), "r"(_a6),
                       "r"(_a7)
                     : "memory");
    return (struct SbiRet){ .error = (i64)_a0, .value = (i64)_a1 };
}

bool sbi_probe_extension(u64 eid)
{
    struct SbiRet r =
        sbi_ecall(SBI_EID_BASE, SBI_BASE_FID_PROBE_EXTENSION, eid, 0, 0, 0, 0, 0);
    return r.error == 0 && r.value != 0;
}

void sbi_legacy_putchar(char c)
{
    // Legacy console_putchar: eid 0x01, fid 0, a0 = char. Returns nothing
    // useful; some older OpenSBI builds clobber a1 — we ignore SbiRet.
    (void)sbi_ecall(0x01, 0, (u64)(u8)c, 0, 0, 0, 0, 0);
}

static bool g_dbcn_probed = false;
static bool g_dbcn_present = false;

static void dbcn_probe_once(void)
{
    if (!g_dbcn_probed) {
        g_dbcn_present = sbi_probe_extension(SBI_EID_DBCN);
        g_dbcn_probed = true;
    }
}

void sbi_putc(char c)
{
    dbcn_probe_once();
    if (g_dbcn_present) {
        (void)sbi_ecall(SBI_EID_DBCN, SBI_DBCN_FID_WRITE_BYTE, (u64)(u8)c, 0, 0,
                        0, 0, 0);
    } else {
        sbi_legacy_putchar(c);
    }
}

void sbi_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            sbi_putc('\r');
        }
        sbi_putc(*s++);
    }
}
