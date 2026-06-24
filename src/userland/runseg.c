// SPDX-License-Identifier: BSD-2-Clause
//
// runseg — the T.3.2 launch-path proof. A U-mode Gleas that loads a
// program off CaraFS *by name* with LoadSeg() and runs it with
// RunCommand() + a command tail, exactly the way the Shell (T.3.3) will
// dispatch a typed command. KERNEL_TEST(loadseg_runcommand) seeds the
// dhrystone ELF onto CaraFS, spawns this launcher, and asserts it exits 0
// — i.e. the loaded-from-file program ran to completion and returned 0.
//
// Verbatim V36+ idiom: OpenLibrary("dos.library") + LoadSeg / RunCommand /
// UnLoadSeg through the generated <proto/dos.h> inline stubs. No cara/*.

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

    BPTR seg = LoadSeg((STRPTR) "dhrystone");
    if (!seg) {
        CloseLibrary(dlib);
        return 20;
    }

    // Run it with a command tail (→ the child's argv); dhrystone ignores
    // its arguments, but this exercises the tail → argv path end to end.
    LONG rc = RunCommand(seg, 8192, (STRPTR) "from-carafs", 11);

    UnLoadSeg(seg);
    CloseLibrary(dlib);
    return (int)rc;
}
