// SPDX-License-Identifier: BSD-2-Clause
//
// CaraShell — the boot console shell (T.3.3, docs/PORTS.md §6). A U-mode
// Gleas that reads a command line from the console (dos Input(), cooked +
// blocking since T.3.1), splits it into a command word + tail, LoadSeg()s
// the command off CaraFS (bare name, then a C/ search), RunCommand()s it
// with the tail, prints the return code, and loops. This closes the
// boot→prompt→type→runs milestone: the launch path (T.3.2) driven by typed
// input rather than a KERNEL_TEST spawn.
//
// Verbatim V36+ idiom through the generated <proto/*> inline stubs; the only
// non-Amiga dependency is libcara's _start. No cara/* includes.

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/libraries.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>

struct DosLibrary *DOSBase; // referenced by <proto/dos.h> stubs

static LONG slen(const char *s)
{
    LONG n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void put(BPTR out, const char *s)
{
    Write(out, (APTR)s, slen(s));
}

// Print a signed decimal (return codes are small; no libc).
static void put_num(BPTR out, LONG v)
{
    char buf[16];
    int i = (int)sizeof(buf);
    buf[--i] = 0;
    unsigned long u = (v < 0) ? (unsigned long)(-v) : (unsigned long)v;
    do {
        buf[--i] = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u != 0 && i > 1);
    if (v < 0) {
        buf[--i] = '-';
    }
    put(out, &buf[i]);
}

static int word_eq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

int main(int argc, char **argv);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    struct Library *dlib = OpenLibrary((STRPTR) "dos.library", 36);
    if (!dlib) {
        return 20;
    }
    DOSBase = (struct DosLibrary *)dlib;

    BPTR out = Output();
    BPTR in = Input();

    put(out, "\nCaraShell ready.  Type a command (e.g. dhrystone), or 'quit'.\n");

    char line[256];
    char cmd[64];
    char path[80];

    for (;;) {
        put(out, "> ");

        LONG n = Read(in, (APTR)line, (LONG)sizeof(line) - 1);
        if (n <= 0) {
            break; // console EOF — no interactive input
        }
        line[n] = 0;

        // Skip leading blanks; find the command word.
        int i = 0;
        while (line[i] == ' ' || line[i] == '\t') {
            i++;
        }
        int c0 = i;
        while (line[i] && line[i] != ' ' && line[i] != '\t' && line[i] != '\n' && line[i] != '\r') {
            i++;
        }
        int clen = i - c0;
        if (clen <= 0) {
            continue; // blank line
        }
        if (clen > (int)sizeof(cmd) - 1) {
            clen = (int)sizeof(cmd) - 1;
        }
        for (int j = 0; j < clen; j++) {
            cmd[j] = line[c0 + j];
        }
        cmd[clen] = 0;

        // The tail is the rest of the line (sans leading blanks, trailing EOL).
        while (line[i] == ' ' || line[i] == '\t') {
            i++;
        }
        int t0 = i;
        while (line[i] && line[i] != '\n' && line[i] != '\r') {
            i++;
        }
        LONG taillen = (LONG)(i - t0);

        if (word_eq(cmd, "quit") || word_eq(cmd, "endcli") || word_eq(cmd, "endshell")) {
            break;
        }

        // LoadSeg: bare name first, then a C/ search.
        BPTR seg = LoadSeg((STRPTR)cmd);
        if (!seg) {
            int p = 0;
            path[p++] = 'C';
            path[p++] = '/';
            for (int j = 0; j < clen && p < (int)sizeof(path) - 1; j++) {
                path[p++] = cmd[j];
            }
            path[p] = 0;
            seg = LoadSeg((STRPTR)path);
        }
        if (!seg) {
            put(out, "Unknown command: ");
            put(out, cmd);
            put(out, "\n");
            continue;
        }

        LONG rc = RunCommand(seg, 8192, (STRPTR)&line[t0], taillen);
        UnLoadSeg(seg);

        if (rc != 0) {
            put(out, cmd);
            put(out, " returned ");
            put_num(out, rc);
            put(out, "\n");
        }
    }

    put(out, "CaraShell exiting.\n");
    CloseLibrary(dlib);
    return 0;
}
