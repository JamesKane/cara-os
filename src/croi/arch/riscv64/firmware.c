// SPDX-License-Identifier: BSD-2-Clause
//
// RISC-V firmware (SBI) early console — the arch_console_* seam (epic H, H.1).
// Before the NS16550 UART driver is installed (and from panic, which must work
// with no printf backend), bytes go to the SBI console: the Debug Console
// extension (DBCN) when the firmware advertises it, else the legacy putchar.
// SBI spec v2.0. This is the only SBI call CaraOS makes today; SBI timer/IPI/
// HSM (for SMP) land later.

#include <cara/arch.h>
#include <cara/types.h>

#define SBI_EID_BASE 0x10
#define SBI_BASE_FID_PROBE_EXTENSION 3
#define SBI_EID_DBCN 0x4442434EUL // 'DBCN'
#define SBI_DBCN_FID_WRITE_BYTE 2
#define SBI_LEGACY_EID_PUTCHAR 0x01

struct SbiRet {
    i64 error;
    i64 value;
};

static struct SbiRet sbi_ecall(u64 eid, u64 fid, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
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
                     : "r"(_a2), "r"(_a3), "r"(_a4), "r"(_a5), "r"(_a6), "r"(_a7)
                     : "memory");
    return (struct SbiRet){ .error = (i64)_a0, .value = (i64)_a1 };
}

static bool sbi_probe_extension(u64 eid)
{
    struct SbiRet r = sbi_ecall(SBI_EID_BASE, SBI_BASE_FID_PROBE_EXTENSION, eid, 0, 0, 0, 0, 0);
    return r.error == 0 && r.value != 0;
}

void arch_console_putc(char c)
{
    static bool probed = false;
    static bool dbcn = false;
    if (!probed) {
        dbcn = sbi_probe_extension(SBI_EID_DBCN);
        probed = true;
    }
    if (dbcn) {
        (void)sbi_ecall(SBI_EID_DBCN, SBI_DBCN_FID_WRITE_BYTE, (u64)(u8)c, 0, 0, 0, 0, 0);
    } else {
        // Legacy console_putchar (eid 0x01); some old OpenSBI clobbers a1.
        (void)sbi_ecall(SBI_LEGACY_EID_PUTCHAR, 0, (u64)(u8)c, 0, 0, 0, 0, 0);
    }
}

void arch_console_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            arch_console_putc('\r');
        }
        arch_console_putc(*s++);
    }
}
