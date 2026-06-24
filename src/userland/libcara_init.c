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

extern int main(int argc, char **argv);

// Max argv entries libcara builds from the command line (T.3.2).
#define CARA_MAX_ARGV 32

// Freestanding requires memset/memcpy to be available — the compiler
// may emit calls to them for struct initialisers / copies (e.g. a large
// `= {0}`). libcara provides them for every Gleas.
void *memset(void *dst, int c, __SIZE_TYPE__ n);
void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n);

void *memset(void *dst, int c, __SIZE_TYPE__ n)
{
    unsigned char *p = (unsigned char *)dst;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

[[noreturn]] void _start(char *cmdline, long cmdlen);

// The kernel hands the new task its command line in a0 (pointer) and a1
// (length) — see Croi_SpawnUserProc (T.3.2). We declare those as the
// _start parameters so the calling convention delivers them directly, then
// tokenise the (writable, NUL-terminated) command line in place into argv.
[[noreturn]] void _start(char *cmdline, long cmdlen)
{
    static const char exec_lib_name[] = "exec.library";
    SysBase = (struct ExecBase *)_libcara_ecall2(SYS_OpenLibrary, (long)exec_lib_name, 0);

    // Build argv from the command line: whitespace-separated tokens, argv[0]
    // = the command name. Spawned-without-a-command-line tasks get cmdline
    // == nullptr / cmdlen == 0 → argc 0.
    static char *argv[CARA_MAX_ARGV];
    int argc = 0;
    if (cmdline && cmdlen > 0) {
        char *s = cmdline;
        char *end = cmdline + cmdlen;
        while (s < end && *s != 0 && argc < CARA_MAX_ARGV - 1) {
            while (s < end && (*s == ' ' || *s == '\t')) {
                s++;
            }
            if (s >= end || *s == 0) {
                break;
            }
            argv[argc++] = s;
            while (s < end && *s != 0 && *s != ' ' && *s != '\t') {
                s++;
            }
            if (s < end && *s != 0) {
                *s++ = 0;
            }
        }
    }
    argv[argc] = nullptr;

    int rc = main(argc, argv);

    // TODO: balance the OpenLibrary above with a CloseLibrary on the
    // happy path (Phase D v0 leaves the implicit open in place; the
    // task exit reaps the resources anyway). When per-task tracked
    // memory lands (tc_MemEntry), we'll close here for cleanliness.

    _libcara_ecall1_noreturn(SYS_EXIT, rc);
}
