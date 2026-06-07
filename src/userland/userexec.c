// SPDX-License-Identifier: BSD-2-Clause
//
// userexec — the LVO.md §9 worked example, adapted for exec.library
// since graphics.library is Phase 4. Demonstrates the full chain:
//
//   - libcara's _start has set the SysBase global by inline-ecall'ing
//     SYS_OpenLibrary("exec.library", 0). main() runs.
//   - main() dereferences sysBase->LibNode.lib_Version to read V36
//     (a plain memory load — the user PT has 0x4000_0000 mapped
//     R+X+U to the .exec_lib physical pages).
//   - main() calls OpenLibrary via the generated <proto/exec.h>
//     inline stub. The stub indexes the negative-side vec table and
//     JALRs to Cara_Trampoline_OpenLibrary (8-byte ecall stub in the
//     same .exec_lib page); the dispatcher routes SYS_OpenLibrary
//     to Croi_OpenLibrary_Impl, which bumps lib_OpenCnt and returns
//     the same base.
//   - main() AllocMems with MEMF_CLEAR, asserts the allocation is
//     zeroed, FreeMems.
//   - main() CloseLibrarys to balance the open it just made.
//   - main() returns; libcara's _start tail-calls SYS_EXIT.
//
// The kernel-side smoke (D3) spawns this ELF and asserts the SYS_EXIT
// status code matches the expected value (CARA_USEREXEC_EXIT_OK).

#include <cara/sysno.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <proto/exec.h>

#define USEREXEC_EXIT_OK 0xCA1A
#define USEREXEC_EXIT_BAD_VERSION 0xBAD1
#define USEREXEC_EXIT_OPEN_FAILED 0xBAD2
#define USEREXEC_EXIT_BASE_MISMATCH 0xBAD3
#define USEREXEC_EXIT_ALLOC_FAILED 0xBAD4
#define USEREXEC_EXIT_NOT_ZEROED 0xBAD5

// Inline ecall for SYS_LOG_WRITE — used to surface progress markers
// in the kernel log alongside the existing kernel-side messages so
// the smoke harness can correlate. Replaces what would otherwise be
// a printf().
static void log_msg(int level, const char *tag, const char *msg)
{
    long len = 0;
    while (msg[len]) {
        len++;
    }
    register long a0 __asm__("a0") = level;
    register long a1 __asm__("a1") = (long)tag;
    register long a2 __asm__("a2") = (long)msg;
    register long a3 __asm__("a3") = len;
    register long a7 __asm__("a7") = SYS_LOG_WRITE;
    __asm__ volatile("ecall" ::"r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
}

int main(void);

int main(void)
{
    log_msg(2, "uexec", "userexec entered");

    // 1. Direct read through SysBase. The user PT has the library
    //    region mapped read-only-execute-user, so a normal load
    //    works — no syscall, no IPC. This is the SASOS payoff
    //    LVO.md §9 step 2 calls out.
    if (!SysBase) {
        return (int)USEREXEC_EXIT_OPEN_FAILED;
    }
    if (SysBase->LibNode.lib_Version != 36) {
        return (int)USEREXEC_EXIT_BAD_VERSION;
    }

    // 2. OpenLibrary via the inline stub from <proto/exec.h>. The
    //    stub dereferences SysBase to load vec[CARA_IDX_OpenLibrary]
    //    and JALRs through it; that target is Cara_Trampoline_OpenLibrary
    //    in the same .exec_lib RX page, which ecalls into the kernel.
    struct Library *lib = OpenLibrary((STRPTR) "exec.library", 0);
    if (!lib) {
        return (int)USEREXEC_EXIT_OPEN_FAILED;
    }
    if (lib != (struct Library *)SysBase) {
        return (int)USEREXEC_EXIT_BASE_MISMATCH;
    }

    // 3. AllocMem with MEMF_CLEAR — the kernel-side dispatcher's
    //    SYS_AllocMem arm calls Croi_AllocMem_Impl, which (since S2)
    //    allocates from the SASOS shared heap (ARCHITECTURE.md §4.3,
    //    0x1_0000_0000). The returned pointer is lower-half RW+U, so —
    //    unlike v0 — user mode can dereference it directly. We verify
    //    MEMF_CLEAR zeroed it and that a write/read round-trips, proving
    //    the shared mapping reaches U-mode. A fault here would Guru the
    //    task and the boot smoke would catch the wrong exit status.
    APTR mem = AllocMem(64, MEMF_CLEAR);
    if (!mem) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_ALLOC_FAILED;
    }
    volatile UBYTE *bytes = (volatile UBYTE *)mem;
    for (int i = 0; i < 64; i++) {
        if (bytes[i] != 0) {
            FreeMem(mem, 64);
            CloseLibrary(lib);
            return (int)USEREXEC_EXIT_NOT_ZEROED;
        }
    }
    for (int i = 0; i < 64; i++) {
        bytes[i] = (UBYTE)(i * 3 + 1);
    }
    for (int i = 0; i < 64; i++) {
        if (bytes[i] != (UBYTE)(i * 3 + 1)) {
            FreeMem(mem, 64);
            CloseLibrary(lib);
            return (int)USEREXEC_EXIT_NOT_ZEROED; // write/read mismatch
        }
    }
    FreeMem(mem, 64);

    // 4. Balance the open. Note: libcara also opened exec.library
    //    at startup, so OpenCnt is still > 0 after this close.
    CloseLibrary(lib);

    log_msg(2, "uexec", "userexec ok");
    return (int)USEREXEC_EXIT_OK;
}
