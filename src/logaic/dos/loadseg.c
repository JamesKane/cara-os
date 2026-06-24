// SPDX-License-Identifier: BSD-2-Clause
//
// dos.library program-load / launch path (T.3.2) — LoadSeg / UnLoadSeg /
// RunCommand. This is the half of process creation the embedded-.incbin
// test path was standing in for: instead of the kernel spawning a blob it
// linked into croi.elf, a program is now read off CaraFS and launched by a
// dos.library call, the way the Shell (T.3.3) and any real app will do it.
//
// `syscall` flavour: each impl runs in the caller Process's context (the
// SYS_Dos_* path in src/croi/syscall/syscall.c), so the internal Open /
// Seek / Read reach CaraFS exactly as a direct dos call from the same
// Process would. See docs/PORTS.md §6 and docs/LOGAIC_DOS.md §7.

#include <cara/alloc.h>
#include <cara/dos_lib.h>
#include <cara/log.h>
#include <cara/sched.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/tasks.h>
#include <exec/types.h>

// A seglist in CaraOS is this kernel-private object; LoadSeg hands its
// BPTR back as the opaque seglist. We keep the whole loaded ELF blob and
// re-map it per child at RunCommand time (SASOS maps each Gleas's text in
// its own page table, so there is no single shared code mapping to share).
#define CARA_SEG_MAGIC 0x43536567u // 'CSeg'

struct CaraSeg {
    u32 cs_Magic;
    u32 cs_Size;      // ELF byte count
    void *cs_Elf;     // shared-heap copy of the ELF image
    char cs_Name[32]; // load name → argv[0]
};

static void set_ioerr(SIPTR code)
{
    struct Process *p = (struct Process *)Sched_Current();
    if (p) {
        p->pr_Result2 = (LONG)code;
    }
}

// Copy up to cap-1 bytes of a C string, NUL-terminating.
static void str_copy(char *dst, usize cap, const char *src)
{
    usize i = 0;
    if (cap == 0) {
        return;
    }
    while (src && src[i] != 0 && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

// LoadSeg(name) → BPTR seglist (0 on failure; IoErr set). Reads the whole
// file off CaraFS and validates it as an RV64 ELF.
BPTR Croi_Dos_LoadSeg_Impl(STRPTR name)
{
    BPTR fh = Croi_Dos_Open_Impl(name, MODE_OLDFILE);
    if (!fh) {
        return BNULL; // Open already set IoErr (ERROR_OBJECT_NOT_FOUND etc.)
    }

    // Size = position after seeking to the end.
    Croi_Dos_Seek_Impl(fh, 0, OFFSET_END);
    LONG size = Croi_Dos_Seek_Impl(fh, 0, OFFSET_CURRENT);
    Croi_Dos_Seek_Impl(fh, 0, OFFSET_BEGINNING);
    if (size <= 0) {
        Croi_Dos_Close_Impl(fh);
        set_ioerr(ERROR_OBJECT_WRONG_TYPE);
        return BNULL;
    }

    void *blob = Croi_AllocShared((usize)size);
    if (!blob) {
        Croi_Dos_Close_Impl(fh);
        set_ioerr(ERROR_NO_FREE_STORE);
        return BNULL;
    }

    LONG got = Croi_Dos_Read_Impl(fh, blob, size);
    Croi_Dos_Close_Impl(fh);
    if (got != size) {
        Croi_Free(blob);
        set_ioerr(ERROR_OBJECT_WRONG_TYPE);
        return BNULL;
    }

    const u8 *b = (const u8 *)blob;
    if (size < 64 || b[0] != 0x7F || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') {
        Croi_Free(blob);
        set_ioerr(ERROR_OBJECT_WRONG_TYPE);
        return BNULL;
    }

    struct CaraSeg *seg = (struct CaraSeg *)Croi_AllocShared(sizeof(struct CaraSeg));
    if (!seg) {
        Croi_Free(blob);
        set_ioerr(ERROR_NO_FREE_STORE);
        return BNULL;
    }
    seg->cs_Magic = CARA_SEG_MAGIC;
    seg->cs_Size = (u32)size;
    seg->cs_Elf = blob;
    str_copy(seg->cs_Name, sizeof(seg->cs_Name), (const char *)name);

    LOG_DEBUG("dos ", "LoadSeg '%s' %u bytes", seg->cs_Name, seg->cs_Size);
    return MKBADDR(seg);
}

// UnLoadSeg(seglist) — free a LoadSeg'd image. BNULL is a no-op.
void Croi_Dos_UnLoadSeg_Impl(BPTR seglist)
{
    if (!seglist) {
        return;
    }
    struct CaraSeg *seg = (struct CaraSeg *)BADDR(seglist);
    if (seg->cs_Magic != CARA_SEG_MAGIC) {
        return; // not ours
    }
    if (seg->cs_Elf) {
        Croi_Free(seg->cs_Elf);
    }
    seg->cs_Magic = 0;
    Croi_Free(seg);
}

// RunCommand(seglist, stack, argptr, argsize) → return code. Spawns the
// seglist as a child Process with a command line "<name> <tail>", blocks
// on the child's exit (via an allocated signal + the kernel join path in
// sys_exit), and returns its exit status. -1 on launch failure.
//
// Classic AmigaOS RunCommand reuses the caller's process image; SASOS maps
// each Gleas's text in its own page table, so we run the command as a fresh
// child task instead. The contract (run seglist with args, get the return
// code) is preserved (docs/PORTS.md §6).
LONG Croi_Dos_RunCommand_Impl(BPTR seglist, LONG stack, STRPTR argptr, LONG argsize)
{
    (void)stack; // v0: fixed user stack size (CARA_USER_STACK_SIZE)
    if (!seglist) {
        set_ioerr(ERROR_OBJECT_NOT_FOUND);
        return -1;
    }
    struct CaraSeg *seg = (struct CaraSeg *)BADDR(seglist);
    if (seg->cs_Magic != CARA_SEG_MAGIC || !seg->cs_Elf) {
        set_ioerr(ERROR_OBJECT_WRONG_TYPE);
        return -1;
    }

    // Build the command line: argv[0] = load name, then the tail.
    char cmd[160];
    usize ci = 0;
    for (usize k = 0; seg->cs_Name[k] != 0 && ci < sizeof(cmd) - 1; k++) {
        cmd[ci++] = seg->cs_Name[k];
    }
    if (argptr && argsize > 0) {
        if (ci < sizeof(cmd) - 1) {
            cmd[ci++] = ' ';
        }
        for (LONG k = 0; k < argsize && ci < sizeof(cmd) - 1; k++) {
            char c = argptr[k];
            if (c == 0 || c == '\n') {
                break;
            }
            cmd[ci++] = c;
        }
    }
    cmd[ci] = 0;

    struct Task *self = Sched_Current();
    i32 sig = Croi_AllocSignal();
    if (sig < 0) {
        set_ioerr(ERROR_NO_FREE_STORE);
        return -1;
    }
    u32 sigmask = 1u << (u32)sig;

    volatile i64 code = -1;
    struct Task *child = Croi_SpawnUserProc(seg->cs_Name, 0, seg->cs_Elf, seg->cs_Size, cmd, ci,
                                            self, sigmask, (i64 *)&code);
    if (!child) {
        Croi_FreeSignal(sig);
        set_ioerr(ERROR_NO_FREE_STORE);
        return -1;
    }

    Croi_Wait(sigmask);
    Croi_FreeSignal(sig);
    return (LONG)code;
}
