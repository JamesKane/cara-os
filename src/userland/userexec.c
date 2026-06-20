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
#include <exec/lists.h>
#include <exec/memory.h>
#include <exec/nodes.h>
#include <exec/ports.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <exec/types.h>
#include <proto/exec.h>

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

    // 5. Balance the open. Note: libcara also opened exec.library
    //    at startup, so OpenCnt is still > 0 after this close.
    CloseLibrary(lib);

    log_msg(2, "uexec", "userexec ok");
    return (int)USEREXEC_EXIT_OK;
}
