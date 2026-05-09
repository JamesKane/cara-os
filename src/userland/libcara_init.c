// SPDX-License-Identifier: BSD-2-Clause
//
// libcara — minimal C runtime for U-mode CaraOS programs.
//
// _start runs before main: bootstrap-opens exec.library via an
// inline ecall (the chicken-and-egg case — we can't go through the
// generated <proto/exec.h> inline stub yet because that
// dereferences the SysBase global, which is precisely what we're
// about to set), assigns SysBase, calls main, then SYS_EXIT on
// return.
//
// `struct ExecBase *SysBase` is the V36+-canonical global every
// user program declares. libcara provides the definition so
// programs linking against this TU don't need to.
//
// Freestanding: no <stdlib.h> / <string.h>. The syscall ABI
// (cara/sysno.h) is the entire surface.

#include <cara/sysno.h>

// Forward struct to avoid pulling exec/execbase.h into a freestanding
// build context that may not yet have <stdint.h> visible. The actual
// type is opaque from libcara's view — we just hold the pointer.
struct ExecBase;

struct ExecBase *SysBase;

// Two-arg ecall — used for SYS_OpenLibrary(name, version).
static long _libcara_ecall2(long n, long a0, long a1)
{
    register long r0 __asm__("a0") = a0;
    register long r1 __asm__("a1") = a1;
    register long r7 __asm__("a7") = n;
    __asm__ volatile("ecall" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    return r0;
}

// One-arg ecall — used for SYS_EXIT(status).
[[noreturn]]
static void _libcara_ecall1_noreturn(long n, long a0)
{
    register long r0 __asm__("a0") = a0;
    register long r7 __asm__("a7") = n;
    __asm__ volatile("ecall" ::"r"(r0), "r"(r7) : "memory");
    for (;;) {
        // SYS_EXIT does not return; spin guard is dead code.
    }
}

extern int main(void);

[[noreturn]] void _start(void);

[[noreturn]] void _start(void)
{
    static const char exec_lib_name[] = "exec.library";
    SysBase = (struct ExecBase *)_libcara_ecall2(SYS_OpenLibrary, (long)exec_lib_name, 0);

    int rc = main();

    // TODO: balance the OpenLibrary above with a CloseLibrary on the
    // happy path (Phase D v0 leaves the implicit open in place; the
    // task exit reaps the resources anyway). When per-task tracked
    // memory lands (tc_MemEntry), we'll close here for cleanliness.

    _libcara_ecall1_noreturn(SYS_EXIT, rc);
}
