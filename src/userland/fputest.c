// SPDX-License-Identifier: BSD-2-Clause
//
// fputest — T.4.1 proof that U-mode floating point works and survives
// context switches. The kernel is soft-float; this Gleas computes in
// `double` (hardware fadd.d/fmul.d, which trap illegal unless the kernel
// enabled sstatus.FS for U-mode) and forces repeated context switches
// mid-computation via Delay(). KERNEL_TEST(fpu_umode) runs it while
// hammering the FPU from the kernel side, so a broken FP save/restore in
// croi_ctx_switch would corrupt this result. Exits 0 iff the maths is
// correct. (Prerequisite substrate for the amiCalc port — docs/PORTS.md T.4.)

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

struct DosLibrary *DOSBase; // referenced by <proto/dos.h> stubs

int main(int argc, char **argv);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct Library *dlib = OpenLibrary((STRPTR) "dos.library", 36);
    if (!dlib) {
        return 30;
    }
    DOSBase = (struct DosLibrary *)dlib;

    // Accumulate in double across many yields. `volatile` defeats constant
    // folding so the arithmetic actually runs at runtime, and keeps `acc`
    // live across the Delay() calls (each Delay yields the CPU, so the
    // partial sum must survive a context switch and come back intact).
    volatile double acc = 0.0;
    volatile double step = 1.25;
    int i;
    for (i = 0; i < 12; i++) {
        acc = acc + step; // FP add, then a switch:
        Delay(2);         // 40 ms of cooperative yielding → real switches
    }

    // acc == 12 * 1.25 == 15.0 ; scale + round-trip through more FP + an
    // FP→int convert (fcvt.l.d) to exercise the conversion path too.
    volatile double scaled = acc * 8.0; // 120.0
    long whole = (long)(scaled + 0.5);  // 120

    CloseLibrary(dlib);
    return (whole == 120) ? 0 : 1;
}
