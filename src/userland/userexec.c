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
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <libraries/iffparse.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/iffparse.h>
#include <proto/utility.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>
#include <utility/utilitybase.h>

#define USEREXEC_EXIT_OK 0xCA1A
#define USEREXEC_EXIT_BAD_VERSION 0xBAD1
#define USEREXEC_EXIT_OPEN_FAILED 0xBAD2
#define USEREXEC_EXIT_BASE_MISMATCH 0xBAD3
#define USEREXEC_EXIT_ALLOC_FAILED 0xBAD4
#define USEREXEC_EXIT_NOT_ZEROED 0xBAD5
#define USEREXEC_EXIT_LIST_FAIL 0xBAD6
#define USEREXEC_EXIT_MSG_FAIL 0xBAD7
#define USEREXEC_EXIT_TASK_FAIL 0xBAD8
#define USEREXEC_EXIT_COPY_FAIL 0xBAD9
#define USEREXEC_EXIT_SEM_FAIL 0xBADA
#define USEREXEC_EXIT_FORBID_FAIL 0xBADB
#define USEREXEC_EXIT_UTIL_FAIL 0xBADC
#define USEREXEC_EXIT_DOS_FAIL 0xBADD
#define USEREXEC_EXIT_SRV_FAIL 0xBADE
#define USEREXEC_EXIT_GFX_FAIL 0xBADF
#define USEREXEC_EXIT_IFF_FAIL 0xBAE1

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

// Fetch the dos handler's MsgPort (SYS_Dos_HandlerPort) — a one-off
// inline ecall used only by the L3.2 server round-trip proof below. The
// eventual server-flavour LVO stubs read this from DOSBase lib-private.
static long dos_handler_port_call(void)
{
    register long r0 __asm__("a0");
    register long r7 __asm__("a7") = SYS_Dos_HandlerPort;
    __asm__ volatile("ecall" : "=r"(r0) : "r"(r7) : "memory");
    return r0;
}

// Exercise the L1 exec.library list LVOs (local flavour) through the
// <proto/exec.h> stubs — the U-mode dispatch into the RX-page impls
// (src/croi/exec_lib/list_ops.c). Returns true if every op behaves.
static void le_newlist(struct List *l)
{
    l->lh_Head = (struct Node *)(void *)&l->lh_Tail;
    l->lh_Tail = nullptr;
    l->lh_TailPred = (struct Node *)(void *)&l->lh_Head;
}

static bool le_order(struct List *l, const char *want)
{
    int i = 0;
    for (struct Node *p = l->lh_Head; p->ln_Succ != nullptr; p = p->ln_Succ, i++) {
        if (want[i] == 0 || p->ln_Name[0] != want[i]) {
            return false;
        }
    }
    return want[i] == 0;
}

static bool list_ops_ok(void)
{
    struct List l;
    struct Node a = { .ln_Name = (char *)"a" };
    struct Node b = { .ln_Name = (char *)"b" };
    struct Node c = { .ln_Name = (char *)"c" };
    struct Node x = { .ln_Name = (char *)"x" };

    le_newlist(&l);
    AddTail(&l, &a);
    AddTail(&l, &b);
    AddTail(&l, &c);
    if (!le_order(&l, "abc")) {
        return false;
    }
    if (FindName(&l, (STRPTR) "b") != &b || FindName(&l, (STRPTR) "z") != nullptr) {
        return false;
    }
    AddHead(&l, &x);
    if (!le_order(&l, "xabc")) {
        return false;
    }
    if (RemHead(&l) != &x || RemTail(&l) != &c || !le_order(&l, "ab")) {
        return false;
    }
    Remove(&a);
    if (!le_order(&l, "b")) {
        return false;
    }

    le_newlist(&l);
    AddTail(&l, &a);
    AddTail(&l, &c);
    Insert(&l, &b, &a); // after a
    if (!le_order(&l, "abc")) {
        return false;
    }

    struct Node hi = { .ln_Name = (char *)"H", .ln_Pri = 10 };
    struct Node mid = { .ln_Name = (char *)"M", .ln_Pri = 5 };
    struct Node lo = { .ln_Name = (char *)"L", .ln_Pri = 0 };
    le_newlist(&l);
    Enqueue(&l, &mid);
    Enqueue(&l, &lo);
    Enqueue(&l, &hi);
    return le_order(&l, "HML");
}

// Referenced by the <proto/utility.h> inline stubs. libcara bootstraps
// only SysBase; UtilityBase is this program's to set from the
// OpenLibrary return (the V36+ idiom), exactly like userintuition does
// for IntuitionBase.
struct UtilityBase *UtilityBase;

// Referenced by the <proto/dos.h> inline stubs (same idiom as
// UtilityBase / IntuitionBase — this program owns the global).
struct DosLibrary *DOSBase;

// Referenced by the <proto/iffparse.h> inline stubs (L10).
struct IFFParseBase *IFFParseBase;

// Referenced by the <proto/graphics.h> inline stubs (same idiom).
struct GfxBase *GfxBase;

// A trivial Hook callback for the CallHookPkt exercise: returns
// object + message so the test can assert the dispatch wired the args
// through. Lives in the program's own U-mode text (0x10000 region),
// which the .lib_text.utility CallHookPkt impl JALRs back into.
static IPTR ue_hook_fn(struct Hook *h, APTR object, APTR message)
{
    (void)h;
    return (IPTR)object + (IPTR)message;
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

    // 4. Exercise the L1 list LVOs (local flavour) through their stubs.
    if (!list_ops_ok()) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_LIST_FAIL;
    }

    // 4b. AllocVec/FreeVec — size-tracked alloc, freed by pointer alone.
    UBYTE *vec = (UBYTE *)AllocVec(100, MEMF_CLEAR);
    if (!vec) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_ALLOC_FAILED;
    }
    for (int i = 0; i < 100; i++) {
        if (vec[i] != 0) {
            FreeVec(vec);
            CloseLibrary(lib);
            return (int)USEREXEC_EXIT_NOT_ZEROED;
        }
        vec[i] = (UBYTE)(i + 1);
    }
    if (vec[0] != 1 || vec[99] != 100) {
        FreeVec(vec);
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_NOT_ZEROED;
    }
    FreeVec(vec);

    // 4c. Messaging round-trip: CreateMsgPort, PutMsg → GetMsg, ReplyMsg
    //     → GetMsg on the reply port, DeleteMsgPort.
    struct MsgPort *port = CreateMsgPort();
    struct MsgPort *reply = CreateMsgPort();
    bool msg_ok = (port != nullptr && reply != nullptr);
    if (msg_ok) {
        struct Message m = { 0 };
        m.mn_Node.ln_Type = NT_MESSAGE;
        m.mn_ReplyPort = reply;
        m.mn_Length = sizeof(m);
        PutMsg(port, &m);
        struct Message *got = GetMsg(port);
        msg_ok = (got == &m);
        if (msg_ok) {
            ReplyMsg(got);
            struct Message *back = GetMsg(reply);
            msg_ok = (back == &m && back->mn_Node.ln_Type == NT_REPLYMSG);
        }
    }
    if (port) {
        DeleteMsgPort(port);
    }
    if (reply) {
        DeleteMsgPort(reply);
    }
    if (!msg_ok) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_MSG_FAIL;
    }

    // 4d. FindTask: NULL is self; this task (spawned as "uexec") is
    //     findable by name; a bogus name is not. The returned Task lives
    //     in kernel memory — opaque to U-mode (don't dereference it); the
    //     name match happens kernel-side.
    struct Task *self = FindTask(nullptr);
    if (!self || FindTask((STRPTR) "uexec") != self ||
        FindTask((STRPTR) "no-such-task-xyz") != nullptr) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_TASK_FAIL;
    }

    // 4e. CopyMem / CopyMemQuick.
    UBYTE csrc[40];
    UBYTE cdst[40];
    for (int i = 0; i < 40; i++) {
        csrc[i] = (UBYTE)(i * 5 + 1);
        cdst[i] = 0;
    }
    CopyMem(csrc, cdst, 40);
    UBYTE qsrc[32];
    UBYTE qdst[32];
    for (int i = 0; i < 32; i++) {
        qsrc[i] = (UBYTE)(i ^ 0x5A);
        qdst[i] = 0;
    }
    CopyMemQuick(qsrc, qdst, 32);
    for (int i = 0; i < 40; i++) {
        if (cdst[i] != csrc[i]) {
            CloseLibrary(lib);
            return (int)USEREXEC_EXIT_COPY_FAIL;
        }
    }
    for (int i = 0; i < 32; i++) {
        if (qdst[i] != qsrc[i]) {
            CloseLibrary(lib);
            return (int)USEREXEC_EXIT_COPY_FAIL;
        }
    }

    // 4f. Signal semaphores — uncontended obtain/nest/release/attempt
    //     (the struct is our own memory, so we can read ss_Owner /
    //     ss_NestCount back). Contended cross-task blocking isn't
    //     exercised here (single task).
    struct SignalSemaphore sem;
    InitSemaphore(&sem);
    bool sem_ok = (sem.ss_Owner == nullptr && sem.ss_NestCount == 0);
    ObtainSemaphore(&sem);
    sem_ok = sem_ok && (sem.ss_Owner == self && sem.ss_NestCount == 1);
    ObtainSemaphore(&sem); // nest
    sem_ok = sem_ok && (sem.ss_NestCount == 2);
    ReleaseSemaphore(&sem);
    sem_ok = sem_ok && (sem.ss_NestCount == 1 && sem.ss_Owner == self);
    ReleaseSemaphore(&sem);
    sem_ok = sem_ok && (sem.ss_NestCount == 0 && sem.ss_Owner == nullptr);
    sem_ok = sem_ok && AttemptSemaphore(&sem); // free → takes it
    sem_ok = sem_ok && (sem.ss_Owner == self && sem.ss_NestCount == 1);
    sem_ok = sem_ok && AttemptSemaphore(&sem); // already owner → nests
    sem_ok = sem_ok && (sem.ss_NestCount == 2);
    ReleaseSemaphore(&sem);
    ReleaseSemaphore(&sem);
    sem_ok = sem_ok && (sem.ss_Owner == nullptr);
    if (!sem_ok) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_SEM_FAIL;
    }

    // 4g. Task-switch control — exercise the U-mode syscall path for
    //     Forbid/Permit/Disable/Enable. Single task, so the only thing
    //     observable here is that the round-trip doesn't fault and that
    //     nested Forbid/Permit + Disable/Enable balance cleanly (the
    //     real "blocks a higher-pri task" semantics is the kernel-side
    //     exec_forbid test). After balancing, a Yield-equivalent path
    //     (CloseLibrary→...→exit) must still make progress.
    Forbid();
    Forbid();
    Permit();
    Permit();
    Disable();
    Enable();
    // If any of the above had wedged the scheduler or faulted, we'd
    // never reach here; reaching here is the pass condition.

    // 4h. utility.library (L2) — open it and exercise the tag-list
    //     walkers + the Hook dispatcher. These are `local` LVOs: each
    //     stub JALRs straight into the .lib_text.utility RX page (no
    //     syscall), operating on TagItem arrays in our own stack memory.
    struct Library *ulib = OpenLibrary((STRPTR) "utility.library", 36);
    if (!ulib) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_UTIL_FAIL;
    }
    UtilityBase = (struct UtilityBase *)ulib;

    bool util_ok = (UtilityBase->ub_LibNode.lib_Version == 36);

    struct TagItem tl[] = {
        { 0x1001, 42 },
        { TAG_IGNORE, 0 },
        { 0x1002, 99 },
        { TAG_DONE, 0 },
    };
    // GetTagData: present (honouring TAG_IGNORE) + default fallback.
    util_ok = util_ok && GetTagData(0x1001, 0, tl) == 42;
    util_ok = util_ok && GetTagData(0x1002, 0, tl) == 99;
    util_ok = util_ok && GetTagData(0x9999, 7, tl) == 7;
    // FindTagItem: hit + miss.
    struct TagItem *ft = FindTagItem(0x1002, tl);
    util_ok = util_ok && ft != nullptr && ft->ti_Data == 99;
    util_ok = util_ok && FindTagItem(0x9999, tl) == nullptr;
    // NextTagItem: skips TAG_IGNORE, returns the two real items then ends.
    struct TagItem *cur = tl;
    struct TagItem *n0 = NextTagItem(&cur);
    struct TagItem *n1 = NextTagItem(&cur);
    struct TagItem *n2 = NextTagItem(&cur);
    util_ok = util_ok && n0 != nullptr && n0->ti_Tag == 0x1001;
    util_ok = util_ok && n1 != nullptr && n1->ti_Tag == 0x1002;
    util_ok = util_ok && n2 == nullptr;
    // TagInArray.
    Tag arr[] = { 0x10, 0x20, 0x30, TAG_DONE };
    util_ok = util_ok && TagInArray(0x20, arr);
    util_ok = util_ok && !TagInArray(0x40, arr);
    // PackBoolTags: 0x1001 true → set bit 0x1; 0x1002 false → clear 0x2.
    struct TagItem bmap[] = { { 0x1001, 0x1 }, { 0x1002, 0x2 }, { TAG_DONE, 0 } };
    struct TagItem btags[] = { { 0x1001, 1 }, { 0x1002, 0 }, { TAG_DONE, 0 } };
    util_ok = util_ok && PackBoolTags(0x2, btags, bmap) == 0x1;
    // CallHookPkt: dispatch through our callback (10 + 5 == 15).
    struct Hook hk = { 0 };
    hk.h_Entry = ue_hook_fn;
    util_ok = util_ok && CallHookPkt(&hk, (APTR)(IPTR)10, (APTR)(IPTR)5) == 15;

    // MapTags (slice 2): remap 0xA→0x100; 0xB not in map → TAG_IGNORE.
    struct TagItem mt[] = { { 0xA, 1 }, { 0xB, 2 }, { TAG_DONE, 0 } };
    struct TagItem mmap[] = { { 0xA, 0x100 }, { TAG_DONE, 0 } };
    MapTags(mt, mmap, MAP_REMOVE_NOT_FOUND);
    util_ok = util_ok && mt[0].ti_Tag == 0x100 && mt[0].ti_Data == 1;
    util_ok = util_ok && mt[1].ti_Tag == TAG_IGNORE;
    // GetTagData should now find the remapped tag and skip the ignored one.
    util_ok = util_ok && GetTagData(0x100, 0, mt) == 1;
    util_ok = util_ok && GetTagData(0xB, 77, mt) == 77;

    // FilterTagItems: AND keeps only 0xB; NOT keeps 0xA and 0xC.
    struct TagItem fi[] = { { 0xA, 1 }, { 0xB, 2 }, { 0xC, 3 }, { TAG_DONE, 0 } };
    Tag fkeep[] = { 0xB, TAG_DONE };
    util_ok = util_ok && FilterTagItems(fi, fkeep, TAGFILTER_AND) == 1;
    util_ok = util_ok && fi[0].ti_Tag == 0xB && fi[1].ti_Tag == TAG_DONE;
    struct TagItem fi2[] = { { 0xA, 1 }, { 0xB, 2 }, { 0xC, 3 }, { TAG_DONE, 0 } };
    util_ok = util_ok && FilterTagItems(fi2, fkeep, TAGFILTER_NOT) == 2;
    util_ok = util_ok && fi2[0].ti_Tag == 0xA && fi2[1].ti_Tag == 0xC && fi2[2].ti_Tag == TAG_DONE;

    // FilterTagChanges: 0xA unchanged (dropped), 0xB changed (kept); with
    // apply the original's 0xB is updated to the new value.
    struct TagItem orig[] = { { 0xA, 5 }, { 0xB, 2 }, { TAG_DONE, 0 } };
    struct TagItem chg[] = { { 0xA, 5 }, { 0xB, 9 }, { TAG_DONE, 0 } };
    FilterTagChanges(chg, orig, TRUE);
    util_ok = util_ok && chg[0].ti_Tag == 0xB && chg[0].ti_Data == 9 && chg[1].ti_Tag == TAG_DONE;
    util_ok = util_ok && orig[1].ti_Data == 9; // applied back

    // Allocating helpers (slice 3, syscall flavour) — AllocateTagItems
    // returns a cleared (all-TAG_DONE) list; CloneTagItems flattens an
    // original (dropping TAG_IGNORE); RefreshTagItemClones restores a
    // modified clone; FreeTagItems releases both.
    struct TagItem *at = AllocateTagItems(4);
    util_ok = util_ok && at != nullptr && at[0].ti_Tag == TAG_DONE;
    at[0].ti_Tag = 0x55;
    at[0].ti_Data = 11;
    at[1].ti_Tag = TAG_DONE;
    util_ok = util_ok && GetTagData(0x55, 0, at) == 11;
    FreeTagItems(at);

    struct TagItem clsrc[] = { { 0xA, 1 }, { TAG_IGNORE, 0 }, { 0xB, 2 }, { TAG_DONE, 0 } };
    struct TagItem *cln = CloneTagItems(clsrc);
    util_ok = util_ok && cln != nullptr;
    util_ok = util_ok && GetTagData(0xA, 0, cln) == 1 && GetTagData(0xB, 0, cln) == 2;
    cln[0].ti_Data = 999; // scribble, then refresh from the original
    RefreshTagItemClones(cln, clsrc);
    util_ok = util_ok && GetTagData(0xA, 0, cln) == 1;
    FreeTagItems(cln);

    CloseLibrary(ulib);
    if (!util_ok) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_UTIL_FAIL;
    }

    // 4i. dos.library (L3.1) — open it, then prove the Process model:
    //     FindTask(NULL) now returns a Task embedded at the front of a
    //     shared-heap struct Process, so it is castable to Process* and
    //     U-readable/writable (the L1 FindTask-opacity gap is gone).
    //     Write pr_Result2 directly and read it back through IoErr()
    //     (the dos syscall reads Sched_Current()->pr_Result2).
    struct Library *dlib = OpenLibrary((STRPTR) "dos.library", 36);
    if (!dlib) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_DOS_FAIL;
    }
    DOSBase = (struct DosLibrary *)dlib;

    bool dos_ok = (DOSBase->dl_lib.lib_Version == 36);
    struct Process *me = (struct Process *)FindTask(nullptr);
    dos_ok = dos_ok && me != nullptr;
    if (me) {
        me->pr_Result2 = 4242;              // a normal U-mode store
        dos_ok = dos_ok && IoErr() == 4242; // dos syscall reads it back
        me->pr_Result2 = 0;
        dos_ok = dos_ok && IoErr() == 0;
    }

    // L3.3 locks: Lock the root volume, DupLock it, then a known-missing
    // name fails with IoErr set. Lock/UnLock go through the dos handler's
    // dispatch (ACTION_LOCATE_OBJECT/FREE_LOCK → CaraFS); DupLock/the
    // root path exercise the FileLock plumbing.
    BPTR rootlock = Lock((STRPTR) ":", SHARED_LOCK);
    dos_ok = dos_ok && rootlock != BNULL;
    BPTR duplock = DupLock(rootlock);
    dos_ok = dos_ok && duplock != BNULL && duplock != rootlock;
    UnLock(duplock);
    BPTR missing = Lock((STRPTR) "no-such-file-zzz", SHARED_LOCK);
    dos_ok = dos_ok && missing == BNULL && IoErr() == ERROR_OBJECT_NOT_FOUND;

    // L3.3b: Examine the root (a directory) then ExNext-walk it. The boot
    // volume seeds at least S/ in the root, so the listing is non-empty.
    struct FileInfoBlock fib;
    dos_ok = dos_ok && Examine(rootlock, &fib) == DOSTRUE;
    dos_ok = dos_ok && fib.fib_DirEntryType > 0; // root is a directory
    int dent = 0;
    while (ExNext(rootlock, &fib) == DOSTRUE) {
        dent++;
        if (dent > 64) {
            break; // guard against a runaway listing
        }
    }
    dos_ok = dos_ok && dent >= 1 && IoErr() == ERROR_NO_MORE_ENTRIES;

    UnLock(rootlock);

    // L3.4 file I/O: create a file, write to it, close; reopen, read it
    // back, seek, read again, close. Exercises Open(NEWFILE/OLDFILE)/
    // Write/Read/Seek/Close over CaraFS.
    static const char wtext[] = "CaraOS dos.library L3.4 file I/O!";
    const LONG wlen = (LONG)(sizeof(wtext) - 1);
    BPTR fh = Open((STRPTR) "uexec-test.txt", MODE_NEWFILE);
    dos_ok = dos_ok && fh != BNULL;
    if (fh) {
        dos_ok = dos_ok && Write(fh, (APTR)wtext, wlen) == wlen;
        Close(fh);
    }
    char rbuf[64];
    BPTR rf = Open((STRPTR) "uexec-test.txt", MODE_OLDFILE);
    dos_ok = dos_ok && rf != BNULL;
    if (rf) {
        LONG n = Read(rf, rbuf, wlen);
        dos_ok = dos_ok && n == wlen;
        for (LONG i = 0; i < wlen; i++) {
            dos_ok = dos_ok && rbuf[i] == wtext[i];
        }
        // Seek back to offset 7 and re-read a byte.
        dos_ok = dos_ok && Seek(rf, 7, OFFSET_BEGINNING) == wlen; // old pos was EOF
        char c = 0;
        dos_ok = dos_ok && Read(rf, &c, 1) == 1 && c == wtext[7];
        Close(rf);
    }

    // L3.5 mutation + info. CreateDir, DeleteFile (removes the L3.4 file
    // and confirms it's gone), Rename (file moved old→new), Info.
    // CaraFS persists across reboots (the boot smoke runs userexec twice
    // on the same volume), so clear any objects a prior boot left behind
    // first — otherwise CreateDir/Rename into fixed names hit EXISTS on
    // the second boot. DeleteFile removes empty dirs too.
    (void)DeleteFile((STRPTR) "uexec-dir");
    (void)DeleteFile((STRPTR) "ren-a.txt");
    (void)DeleteFile((STRPTR) "ren-b.txt");
    BPTR ndir = CreateDir((STRPTR) "uexec-dir");
    dos_ok = dos_ok && ndir != BNULL;
    if (ndir) {
        UnLock(ndir);
    }
    dos_ok = dos_ok && DeleteFile((STRPTR) "uexec-test.txt") == DOSTRUE;
    dos_ok = dos_ok && Lock((STRPTR) "uexec-test.txt", SHARED_LOCK) == BNULL;

    BPTR mk = Open((STRPTR) "ren-a.txt", MODE_NEWFILE);
    if (mk) {
        Write(mk, (APTR) "x", 1);
        Close(mk);
    }
    dos_ok = dos_ok && Rename((STRPTR) "ren-a.txt", (STRPTR) "ren-b.txt") == DOSTRUE;
    dos_ok = dos_ok && Lock((STRPTR) "ren-a.txt", SHARED_LOCK) == BNULL;
    BPTR rb = Lock((STRPTR) "ren-b.txt", SHARED_LOCK);
    dos_ok = dos_ok && rb != BNULL;
    if (rb) {
        UnLock(rb);
    }

    struct InfoData info;
    BPTR il = Lock((STRPTR) ":", SHARED_LOCK);
    dos_ok = dos_ok && Info(il, &info) == DOSTRUE && info.id_NumBlocks > 0 &&
             info.id_BytesPerBlock > 0;
    if (il) {
        UnLock(il);
    }

    // L3.6 process I/O + console + Delay. Output()/Input() return the
    // Process's standard streams (lazily-created console handles, stable
    // across calls). Write to stdout (log-backed); a stdin Read returns
    // EOF (0) in v0. Delay(1) exercises the Croi_Time spin-yield shim.
    BPTR out = Output();
    dos_ok = dos_ok && out != BNULL && Output() == out; // idempotent
    static const char hello[] = "userexec: hello via dos.library Output()\n";
    LONG hlen = (LONG)(sizeof(hello) - 1);
    dos_ok = dos_ok && Write(out, (APTR)hello, hlen) == hlen;
    BPTR in = Input();
    char inbuf[8];
    dos_ok = dos_ok && in != BNULL && in != out && Read(in, inbuf, sizeof(inbuf)) == 0;
    Delay(1); // ~20 ms; returns void — just prove the path is wired

    // L10: iffparse.library — write a raw IFF FORM ILBM (a BMHD + a BODY
    // chunk) via dos, reopen it, and parse it back through iffparse:
    // StopChunk(BODY) + ParseIFF(SCAN) stops at the BODY, ReadChunkBytes
    // returns its contents. Proves the read walk over a real dos stream.
    bool iff_ok = false;
    struct Library *ilib = OpenLibrary((STRPTR) "iffparse.library", 36);
    if (ilib) {
        IFFParseBase = (struct IFFParseBase *)ilib;
        iff_ok = (((struct Library *)IFFParseBase)->lib_Version == 36);

        static const UBYTE iffbytes[] = {
            'F', 'O', 'R', 'M', 0,   0,   0, 30,             // FORM, size 30
            'I', 'L', 'B', 'M',                              // form type
            'B', 'M', 'H', 'D', 0,   0,   0, 4,  1, 2, 3, 4, // BMHD chunk (4 bytes)
            'B', 'O', 'D', 'Y', 0,   0,   0, 6,              // BODY chunk (6 bytes)
            'h', 'e', 'l', 'l', 'o', '!',
        };
        BPTR wf = Open((STRPTR) "uexec.iff", MODE_NEWFILE);
        iff_ok = iff_ok && wf != BNULL;
        if (wf) {
            iff_ok = iff_ok &&
                     Write(wf, (APTR)iffbytes, (LONG)sizeof(iffbytes)) == (LONG)sizeof(iffbytes);
            Close(wf);
        }

        BPTR rf2 = Open((STRPTR) "uexec.iff", MODE_OLDFILE);
        iff_ok = iff_ok && rf2 != BNULL;
        if (rf2) {
            struct IFFHandle *iff = AllocIFF();
            iff_ok = iff_ok && iff != nullptr;
            if (iff) {
                InitIFFasDOS(iff);
                iff->iff_Stream = (IPTR)(uptr)rf2;
                iff_ok = iff_ok && OpenIFF(iff, IFFF_READ) == 0;
                iff_ok = iff_ok && StopChunk(iff, MAKE_ID('I', 'L', 'B', 'M'),
                                             MAKE_ID('B', 'O', 'D', 'Y')) == 0;
                iff_ok = iff_ok && ParseIFF(iff, IFFPARSE_SCAN) == 0;
                struct ContextNode *cn = CurrentChunk(iff);
                iff_ok = iff_ok && cn != nullptr && cn->cn_ID == MAKE_ID('B', 'O', 'D', 'Y') &&
                         cn->cn_Size == 6;
                char body[8] = { 0 };
                iff_ok = iff_ok && ReadChunkBytes(iff, body, 6) == 6;
                iff_ok = iff_ok && body[0] == 'h' && body[4] == 'o' && body[5] == '!';
                CloseIFF(iff);
                FreeIFF(iff);
            }
            Close(rf2);
        }
        (void)DeleteFile((STRPTR) "uexec.iff");
        CloseLibrary(ilib);
    }

    CloseLibrary(dlib);
    if (!dos_ok) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_DOS_FAIL;
    }
    if (!iff_ok) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_IFF_FAIL;
    }

    // 4j. dos server-flavour call path (L3.2) — drive a full DosPacket
    //     round-trip to the kernel-resident dos handler task: build a
    //     StandardPacket on our stack, PutMsg it to the handler port,
    //     block on our reply port, and read the echoed result back. This
    //     is the U-mode → PutMsg → server → ReplyMsg path the real packet
    //     LVOs (L3.3+) build on. ACTION_NIL just echoes dp_Arg1.
    struct MsgPort *rp = CreateMsgPort();
    struct MsgPort *hp = (struct MsgPort *)(IPTR)dos_handler_port_call();
    bool srv_ok = (rp != nullptr && hp != nullptr);
    if (srv_ok) {
        struct StandardPacket sp = { 0 };
        sp.sp_Msg.mn_Node.ln_Type = NT_MESSAGE;
        sp.sp_Msg.mn_Node.ln_Name = (char *)&sp.sp_Pkt; // AmigaDOS convention
        sp.sp_Msg.mn_Length = sizeof(sp);
        sp.sp_Msg.mn_ReplyPort = rp;
        sp.sp_Pkt.dp_Link = &sp.sp_Msg;
        sp.sp_Pkt.dp_Port = rp;
        sp.sp_Pkt.dp_Type = ACTION_NIL;
        sp.sp_Pkt.dp_Arg1 = 0x5AFE;

        PutMsg(hp, &sp.sp_Msg);
        (void)WaitPort(rp);
        (void)GetMsg(rp);

        srv_ok = (sp.sp_Pkt.dp_Res1 == 0x5AFE && sp.sp_Pkt.dp_Res2 == 0);
    }
    if (rp) {
        DeleteMsgPort(rp);
    }
    if (!srv_ok) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_SRV_FAIL;
    }

    // 4k. graphics.library (L4.1 + L4.2). Open it and prove the base
    //     (lib_Version == 36). Then AllocBitMap an off-screen chunky
    //     surface, InitRastPort onto it, SetRast it to a palette pen, and
    //     read the pixels back from the shared-heap surface (Planes[0]) —
    //     deterministic, screen-independent (docs/DATH_GRAPHICS.md §5).
    struct Library *glib = OpenLibrary((STRPTR) "graphics.library", 36);
    bool gfx_ok = glib != nullptr;
    if (gfx_ok) {
        GfxBase = (struct GfxBase *)glib;
        gfx_ok = GfxBase->LibNode.lib_Version == 36;

        // depth 32 → RGBA8888 (4 bpp); 8x4 = 32 pixels.
        struct BitMap *bm = AllocBitMap(8, 4, 32, BMF_CLEAR, nullptr);
        gfx_ok = gfx_ok && bm != nullptr;
        if (bm) {
            gfx_ok = gfx_ok && bm->BytesPerRow == 32 && bm->Rows == 4 && bm->Depth == 32;
            struct RastPort grp;
            InitRastPort(&grp);
            gfx_ok = gfx_ok && grp.FgPen == 1 && grp.DrawMode == JAM2; // V36 defaults
            grp.BitMap = bm;
            SetRast(&grp, 2); // palette pen 2 = red → 0xFFFF0000 in RGBA8888
            ULONG *gpx = (ULONG *)bm->Planes[0];
            for (int gi = 0; gi < 8 * 4; gi++) {
                if (gpx[gi] != 0xFFFF0000u) {
                    gfx_ok = false;
                }
            }

            // L4.3 pen state + primitives. SetAPen blue (palette 4 →
            // 0xFF0000FF), then WritePixel/ReadPixel, RectFill, Move/Draw
            // — verify via the shared-heap surface (8 px/row).
            SetAPen(&grp, 4);
            gfx_ok = gfx_ok && WritePixel(&grp, 0, 0) == 0;
            gfx_ok = gfx_ok && (ULONG)ReadPixel(&grp, 0, 0) == 0xFF0000FFu;
            gfx_ok = gfx_ok && ReadPixel(&grp, 100, 100) == -1; // out of bounds
            gfx_ok = gfx_ok && WritePixel(&grp, 100, 100) == -1;
            RectFill(&grp, 1, 1, 2, 2); // 2x2 blue block, inclusive corners
            gfx_ok = gfx_ok && gpx[1 * 8 + 1] == 0xFF0000FFu && gpx[2 * 8 + 2] == 0xFF0000FFu;
            Move(&grp, 0, 3);
            Draw(&grp, 7, 3); // horizontal blue line across row 3
            gfx_ok = gfx_ok && gpx[3 * 8 + 5] == 0xFF0000FFu && grp.cp_x == 7 && grp.cp_y == 3;

            // L4.4 blits. BltBitMap a full copy bm→bm2 (verify pixel-for-
            // pixel), then ClipBlit + BltBitMapRastPort via a RastPort
            // over bm2 (verify the copied corner pixels).
            struct BitMap *bm2 = AllocBitMap(8, 4, 32, BMF_CLEAR, nullptr);
            gfx_ok = gfx_ok && bm2 != nullptr;
            if (bm2) {
                ULONG *gpx2 = (ULONG *)bm2->Planes[0];
                BltBitMap(bm, 0, 0, bm2, 0, 0, 8, 4, 0xC0, 0xFF, nullptr);
                for (int gi = 0; gi < 8 * 4; gi++) {
                    if (gpx2[gi] != gpx[gi]) {
                        gfx_ok = false;
                    }
                }
                struct RastPort grp2;
                InitRastPort(&grp2);
                grp2.BitMap = bm2;
                SetRast(&grp2, 0); // clear to black
                ClipBlit(&grp, 0, 0, &grp2, 5, 1, 2, 2, 0xC0);
                gfx_ok = gfx_ok && gpx2[1 * 8 + 5] == gpx[0]; // bm(0,0) → bm2(5,1)
                BltBitMapRastPort(bm, 0, 0, &grp2, 7, 3, 1, 1, 0xC0);
                gfx_ok = gfx_ok && gpx2[3 * 8 + 7] == gpx[0]; // bm(0,0) → bm2(7,3)
                FreeBitMap(bm2);
            }

            // L4.5 text + fonts. OpenFont (single v0 system font),
            // SetFont, TextLength, then Text 'A' into a 16x8 bitmap and
            // verify some-but-not-all pixels lit (a real glyph) + cursor.
            struct TextAttr ta = { (STRPTR) "topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
            struct TextFont *tf = OpenFont(&ta);
            gfx_ok = gfx_ok && tf != nullptr && tf->tf_YSize == 8;
            struct BitMap *btf = AllocBitMap(16, 8, 32, BMF_CLEAR, nullptr);
            gfx_ok = gfx_ok && btf != nullptr;
            if (tf && btf) {
                struct RastPort grp3;
                InitRastPort(&grp3);
                grp3.BitMap = btf;
                SetFont(&grp3, tf);
                gfx_ok = gfx_ok && grp3.Font == tf && grp3.TxHeight == 8;
                gfx_ok = gfx_ok && TextLength(&grp3, (STRPTR) "AB", 2) == 16;
                SetRast(&grp3, 0); // black
                SetAPen(&grp3, 1); // white
                Move(&grp3, 0, 0);
                Text(&grp3, (STRPTR) "A", 1);
                ULONG *gpx3 = (ULONG *)btf->Planes[0];
                int lit = 0;
                for (int gi = 0; gi < 16 * 8; gi++) {
                    if (gpx3[gi] == 0xFFFFFFFFu) {
                        lit++;
                    }
                }
                gfx_ok = gfx_ok && lit > 0 && lit < 16 * 8 && grp3.cp_x == 8;
            }
            if (tf) {
                CloseFont(tf);
            }
            if (btf) {
                FreeBitMap(btf);
            }

            // L4.6 area fill. Fill a right triangle (2,2)-(13,2)-(13,13)
            // into a 16x16 bitmap and check an interior pixel is set, an
            // exterior pixel is not.
            struct BitMap *bar = AllocBitMap(16, 16, 32, BMF_CLEAR, nullptr);
            gfx_ok = gfx_ok && bar != nullptr;
            if (bar) {
                struct AreaInfo ai;
                UBYTE areabuf[5 * 8]; // 8 vectors
                struct RastPort grp4;
                InitRastPort(&grp4);
                grp4.BitMap = bar;
                grp4.AreaInfo = &ai;
                InitArea(&ai, areabuf, 8);
                SetRast(&grp4, 0); // black
                SetAPen(&grp4, 1); // white
                gfx_ok = gfx_ok && AreaMove(&grp4, 2, 2) == 0;
                gfx_ok = gfx_ok && AreaDraw(&grp4, 13, 2) == 0;
                gfx_ok = gfx_ok && AreaDraw(&grp4, 13, 13) == 0;
                gfx_ok = gfx_ok && AreaEnd(&grp4) == 0;
                ULONG *gpx4 = (ULONG *)bar->Planes[0];
                gfx_ok = gfx_ok && gpx4[6 * 16 + 10] == 0xFFFFFFFFu; // interior
                gfx_ok = gfx_ok && gpx4[11 * 16 + 3] != 0xFFFFFFFFu; // exterior
                FreeBitMap(bar);
            }

            FreeBitMap(bm);
        }
        CloseLibrary(glib);
    }
    if (!gfx_ok) {
        CloseLibrary(lib);
        return (int)USEREXEC_EXIT_GFX_FAIL;
    }

    // 5. Balance the open. Note: libcara also opened exec.library
    //    at startup, so OpenCnt is still > 0 after this close.
    CloseLibrary(lib);

    log_msg(2, "uexec", "userexec ok");
    return (int)USEREXEC_EXIT_OK;
}
