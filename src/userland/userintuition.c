// SPDX-License-Identifier: BSD-2-Clause
//
// userintuition — the I3 worked example for intuition.library, the
// twin of userexec.c for exec.library. Demonstrates the full U-mode →
// intuition.library → Leargas chain:
//
//   - libcara's _start has set SysBase by inline-ecall'ing
//     SYS_OpenLibrary("exec.library", 0). main() runs.
//   - main() OpenLibrary("intuition.library", 0) via the <proto/exec.h>
//     inline stub (an exec.library syscall) and assigns the IntuitionBase
//     global the <proto/intuition.h> stubs index off.
//   - main() OpenWindow(&nw) with NewWindow.Screen = nullptr — the
//     <proto/intuition.h> stub loads vec[CARA_IDX_OpenWindow] (a
//     trampoline in the shared 0x4000_0000 RX page) and ecalls; the
//     dispatcher routes SYS_OpenWindow → Croi_OpenWindow_Impl →
//     Leargas_OpenWindow, which opens onto the active (boot/test) screen
//     and returns a SASOS Window pointer valid in U-mode.
//   - main() CloseWindow(w), CloseLibrary(intuition), returns OK;
//     libcara tail-calls SYS_EXIT.
//
// The kernel-side smoke (test_userintuition.c) sets up an active screen
// first (the boot path opens none under -nographic), spawns this ELF,
// and asserts the SYS_EXIT status is USERINT_EXIT_OK.

#include <cara/sysno.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#define USERINT_EXIT_OK 0xC1A7
#define USERINT_EXIT_NO_SYSBASE 0xBAD1
#define USERINT_EXIT_NO_INTUITION 0xBAD2
#define USERINT_EXIT_OPENWINDOW_FAILED 0xBAD3
#define USERINT_EXIT_BASE_MISMATCH 0xBAD4

// Referenced by the <proto/intuition.h> inline stubs. libcara only
// bootstraps SysBase; IntuitionBase is this program's to set from the
// OpenLibrary return (the V36+ idiom).
struct IntuitionBase *IntuitionBase;

// Inline ecall for SYS_LOG_WRITE — progress markers in the kernel log,
// mirroring userexec.c.
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
    log_msg(2, "uintu", "userintuition entered");

    if (!SysBase) {
        return (int)USERINT_EXIT_NO_SYSBASE;
    }

    // 1. Open intuition.library via the exec.library OpenLibrary stub.
    struct Library *ilib = OpenLibrary((STRPTR) "intuition.library", 0);
    if (!ilib) {
        return (int)USERINT_EXIT_NO_INTUITION;
    }
    IntuitionBase = (struct IntuitionBase *)ilib;
    if (IntuitionBase->LibNode.lib_Version != 36) {
        CloseLibrary(ilib);
        return (int)USERINT_EXIT_BASE_MISMATCH;
    }

    // 2. OpenWindow on the active screen (Screen = nullptr). IDCMPFlags
    //    zero keeps it UserPort-free — this smoke proves the LVO chain,
    //    not the IDCMP path. The returned Window is a shared-heap SASOS
    //    pointer, dereferenceable here in U-mode.
    char title[] = "Clar";
    struct NewWindow nw = {
        .LeftEdge = 20,
        .TopEdge = 20,
        .Width = 120,
        .Height = 60,
        .DetailPen = 0,
        .BlockPen = 1,
        .IDCMPFlags = 0,
        .Flags = WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET,
        .FirstGadget = nullptr,
        .Title = (UBYTE *)title,
        .Screen = nullptr,
    };
    struct Window *w = OpenWindow(&nw);
    if (!w) {
        CloseLibrary(ilib);
        return (int)USERINT_EXIT_OPENWINDOW_FAILED;
    }

    // 3. Tear down and balance the open.
    CloseWindow(w);
    CloseLibrary(ilib);

    log_msg(2, "uintu", "userintuition ok");
    return (int)USERINT_EXIT_OK;
}
