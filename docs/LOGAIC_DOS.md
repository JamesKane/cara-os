<!-- SPDX note: markdown is licence-exempt (docs/PRINCIPLES.md §1). -->
# Logaic / dos.library — Phase 3 L3 design

The keystone epic of Phase 3. `dos.library` is the AmigaDOS API
(`Open`/`Read`/`Lock`/`Examine`/…) that the editor, paint program, and
file manager all sit on, and it is the first CaraOS library whose
real work crosses the library↔Gleas seam. This document fixes the
three architectural decisions L3 needs before code, maps the AmigaDOS
surface onto the existing CaraFS mount, and lays out the slice plan.

Read first: `docs/PHASE3.md` (the epic order + the three locked Phase-3
decisions), `docs/LVO.md` §5.3 (the `server` flavour) and §12 (the
deferred marshalling), `docs/LOGAIC_BOOT.md` (today's `Croi_Fs_*`
stopgap this epic retires), `docs/CARAFS.md` (the filesystem L3 drives).

---

## 1. Scope

In: the `dos.library` LVO surface a recompiled V36+ AmigaDOS program
needs — file I/O (`Open`/`Close`/`Read`/`Write`/`Seek`), locks
(`Lock`/`UnLock`/`DupLock`/`CurrentDir`), directory examination
(`Examine`/`ExNext`/`ExAll`), namespace mutation (`CreateDir`/
`DeleteFile`/`Rename`/`SetProtection`/`SetComment`/`SetFileDate`),
volume info (`Info`), and the process/CLI bits the apps touch
(`Output`/`Input`, `Write`/`Read` to the console, `Delay`, `IoErr`).
The AmigaDOS **packet** machinery (`DosPacket`, the handler `MsgPort`)
and a minimal **console handler** for stdin/stdout.

Out: networking handlers, `assign`/multi-volume namespaces beyond a
single boot volume + a console, BCPL overlay/`RunCommand` segment
loading, pattern matching (`MatchFirst`/`ParsePattern`) past what the
file manager needs. Declared as `##pad_run` stubs and filled on demand
(the `docs/LVO_COVERAGE.md` policy).

The locked Phase-3 decision (PHASE3.md): **dos.library is an AmigaDOS
handler Gleas — `server` flavour for the packet ops.** This doc says
how.

---

## 2. The three decisions

### 2.1 BPTR / BSTR — real BCPL pointers, widened to pointer size

AmigaDOS passes BCPL pointers (`BPTR`) and BCPL strings (`BSTR`) at the
ABI. A `BPTR` is a machine address shifted right by 2 (a long-word
address); a `BSTR` is a `BPTR` to a length-prefixed string (first byte
= length, then the characters, **not** NUL-terminated).

**Decision (honouring the representation already chosen in
`<exec/types.h>`): a `BPTR` is a *real* pointer-width value — `void *`,
with no `>>2` BCPL shift.** `BADDR`/`MKBADDR` are therefore identity
casts. (The earlier draft proposed `addr >> 2`; the in-tree
`typedef void *BPTR` predates this doc, so dos conforms to it rather than
redefining a load-bearing exec type.) Pointer width is mandatory anyway —
Sv39 addresses don't fit in 32 bits — so `BSTR` is also widened from the
stale `typedef LONG BSTR` to `typedef BPTR BSTR`.

```c
// <exec/types.h>: typedef void *BPTR;  typedef BPTR BSTR;
#define BADDR(b)   ((void *)(b))    // <dos/dos.h>
#define MKBADDR(p) ((BPTR)(p))
#define BNULL      ((BPTR)0)
```

A recompiled V36 program that does `BADDR(lock)` to read `fl_Key` gets a
real pointer (identity). Only code doing raw numeric BPTR arithmetic
(`lock << 2`) breaks — already broken under SASOS, accepted
cleanroom-recompile class. **Conversion happens only at the dos
boundary:** the handler and stubs work in native pointers internally and
expose BPTRs (which here *are* native pointers) at the ABI edge. BSTR↔C
conversion gets helpers (`Logaic_BstrToC`/`Logaic_CToBstr`) at that edge.

### 2.2 Process vs Task — the Gleas Task moves into a shared-heap Process

`struct Process` extends `struct Task`: `pr_Task` is its first field, so
`(struct Process *)FindTask(NULL)` is the canonical AmigaDOS idiom and
programs read `pr_Result2` (the `IoErr` value), `pr_CurrentDir`,
`pr_CIS`/`pr_COS` (console streams) straight off it.

That collides with a known Phase-3 gap (HANDOFF / the L1 slice-4 note):
**U-mode Task structs are kernel-heap-resident today, so `FindTask`'s
result is opaque to U-mode** (`userexec` must not deref it). dos forces
the fix, and the fix *is* the Process model:

**Decision: a U-mode Gleas's `struct Task` is embedded at the front of a
`struct Process` allocated in the SASOS shared heap.** `Croi_SpawnUserTask`
allocates the `Process` via `Croi_AllocShared` (it uses `Croi_Alloc`,
kernel heap, today) and the scheduler runs `&proc->pr_Task`. Because
`sstatus.SUM=1`, the kernel still reads/writes the lower-half Process
from S-mode, and U-mode reads it through the same pointer. `FindTask`
then returns a pointer a program can legally cast to `struct Process *`.
kmain and kernel-only tasks stay kernel-resident (they're never dos
Processes); only U-mode Gleasanna get a Process. This single change
resolves *both* the Process-vs-Task question and the FindTask-opacity
gap.

`IoErr()` / `SetIoErr()` read/write `pr_Result2` — pure loads/stores on
the caller's own (now shared) Process, so they are `local` flavour.

### 2.3 Architecture — a dos handler server task, `server` packet ops

> **v0 update (L3.2):** the handler runs as a **kernel-resident server
> task** (`Croi_SpawnKernelTask("dos.handler")`), not a U-mode Gleas. It
> owns the CaraFS mount (Croi/S-mode), so co-locating it there lets the
> real actions call `Carafs_*` directly — no handler-only FS syscall
> surface needed. The reusable, load-bearing part is the **U-mode
> server-flavour call path** (PutMsg → server task → ReplyMsg), which is
> identical whether the server is kernel- or U-mode; apps never see the
> seam (SASOS). A U-mode handler Gleas remains a future option. The rest
> of this section describes the original U-mode framing; substitute
> "kernel server task" for "handler Gleas" for v0.

```
  app  ──OpenLibrary("dos.library")──►  DosLibrary base (shared heap)
   │
   │  Open()/Read()/Lock()/…  (server-flavour LVO stub in the RX page)
   ▼
  build a DosPacket  ──PutMsg(handler port)──►  Logaic dos handler Gleas
   │                                               │  GetMsg, dispatch by
   │  WaitPort(my reply port)                      │  dp_Type (ACTION_*)
   ▼                                               ▼
  demarshal dp_Res1/dp_Res2  ◄──ReplyMsg──   Carafs_* via FS syscalls
```

The dos handler is a real **U-mode Logaic Gleas** that owns a `MsgPort`,
receives `DosPacket`s, and performs the I/O. It reaches CaraFS — which
lives in Croi (S-mode, owns NVMe + the mount `g_carafs`) — through a
**handler-only, cnode/lock-level kernel FS syscall surface**: a widening
of today's name-in-root `Croi_Fs_Read/Write` to the operations
`Carafs_*` already exposes (`CnodeStat`, `FileRead/Write`,
`DirLookup/Create/Next/Remove`). Those syscalls are *not* the retired
stopgap — they're the handler's substrate; the stopgap is the
*app-facing* `SYS_Fs_Read/Write` name-in-root shortcut Clar calls today.

Why a Gleas and not just kernel `syscall`-flavour dos:
- It's the locked Phase-3 decision and the faithful AmigaDOS model
  (handlers are processes; the packet IS the protocol).
- It builds the **server-flavour call path** (LVO.md §12, still
  deferred) that L4 (`graphics.library` GPU ops) and L6 (`OpenDevice`/
  `DoIO` devices) need anyway. dos is the cheapest place to build it.
- It serialises all FS mutation through one task — the natural place
  for the CaraFS journal's single-writer assumption.

Cost: two hops per op (PutMsg + a FS syscall inside the handler). v0 is
synchronous (LVO.md §12 Q3); perf is a later concern.

---

## 3. The server-flavour call path (built generically here)

This is reusable infrastructure, not dos-specific — so build it cleanly:

1. **lvo-gen real `server` stub.** Replace the `[[gnu::error]]` /
   `Croi_LvoServerStub` placeholders. For a `server` row the generated
   stub: allocates a `DosPacket`-shaped message (a per-task pool /
   stack), marshals args by declaration order (LVO.md §12 Q2 rule:
   natural-aligned fields, SASOS pointers verbatim), reads the
   library-private handler `MsgPort` pointer, `PutMsg`s it, `WaitPort`s
   the **per-task reply port**, demarshals the return, returns it.
2. **Per-task reply port (libcara).** LVO.md §12 Q4: one reply `MsgPort`
   per Gleas, shared by every server stub, created at task startup,
   freed at exit. Add it to `src/userland/libcara_init.c`. Synchronous
   dispatch (Q3) → identity-assert the dequeued reply is the one sent.
3. **The message kind.** dos uses the canonical `struct DosPacket` /
   `struct StandardPacket` with `dp_Type` (ACTION_*), `dp_Arg1..7`,
   `dp_Res1`/`dp_Res2`. The dos server stubs build a DosPacket rather
   than a generic marshalled blob, because the handler dispatches on
   `dp_Type` and apps that send raw packets (`DoPkt`/`SendPkt`) must
   interoperate.
4. **`MKL_SERVER_PORT_KOBJ`** (already in the MakeLibrary tag set,
   LVO.md §12 Q1): dos.library is constructed with the handler Gleas's
   MsgPort so the stubs find it in library-private state.

`cruth.library` already *declares* server rows but they stay stubbed;
dos is the first to light the path up.

---

## 4. CaraFS mapping

AmigaDOS objects map cleanly onto the existing cnode-level mount API
(`include/cara/carafs.h`); cnode (== block number) is the inode id.

| AmigaDOS                     | CaraFS substrate |
|------------------------------|------------------|
| `FileLock.fl_Key`            | cnode (`u64`) of the locked object |
| `Lock`/`UnLock`/`DupLock`    | `Carafs_DirLookup` + alloc/free a shared-heap `FileLock` |
| `Open`/`Read`/`Write`/`Seek` | `Carafs_FileRead`/`Carafs_FileWrite` at `fh_Pos`; `FileHandle` holds cnode + offset |
| `Examine`/`ExNext`           | `Carafs_CnodeStat` + `Carafs_DirNext` (the `CarafsDirCursor` is the `ExNext` resume token) → fill `FileInfoBlock` |
| `CreateDir`/`DeleteFile`     | `Carafs_DirCreate` / `Carafs_DirRemove` |
| `Rename`                     | `Carafs_DirCreate` at dest + `Carafs_DirRemove` at src (one journal txn) |
| `SetProtection`/`SetComment`/`SetFileDate` | `CarafsCnode.fib_protection` / xattr / timestamps |
| `Info`                       | superblock free/total blocks |
| `DateStamp`                  | `Croi_Time` → AmigaDOS days/mins/ticks since 1978-01-01 |

`FileLock` and `FileHandle` are shared-heap structs the handler
allocates and hands back as `BPTR`s (`MKBADDR`). `fl_Task` /
`fh_Type` point at the handler's MsgPort.

---

## 5. Slice plan (dependency-ordered, each ends green + committed)

- **L3.1 — ABI + Process model.** Headers `dos/dos.h` (BPTR/BSTR +
  BADDR/MKBADDR, `MODE_*`, `*_LOCK`, `OFFSET_*`, `ERROR_*`, `DateStamp`,
  `FileInfoBlock`, `FileLock`, `FileHandle`), `dos/dosextens.h`
  (`Process`, `DosLibrary`, `DosPacket`, `StandardPacket`, `ACTION_*`).
  `dos.conf` declaring the full V36 dos autodoc (real rows for what later
  slices fill, rest `##pad_run`). Logaic constructs `dos.library` at
  boot. **The Task→shared-heap-Process move** + `FindTask` returns it +
  `IoErr`/`SetIoErr` (`local`, over `pr_Result2`). *Done when:* a
  verbatim-V36 program opens dos.library, does
  `(struct Process *)FindTask(NULL)` and reads `pr_*` without faulting;
  `SetIoErr(42); IoErr()==42`. Coverage row appears for dos.
- **L3.2 — server call path + handler skeleton.** Generic server stub in
  lvo-gen; libcara per-task reply port; spawn the Logaic dos handler
  Gleas owning a MsgPort; one packet round-trip end to end (pick the
  simplest real op). *Done when:* an app-side server-flavour LVO does a
  full PutMsg→handler→ReplyMsg→demarshal and returns the right value.
- **L3.3 — locks + examine.** `Lock`/`UnLock`/`DupLock`/`CurrentDir`;
  `Examine`/`ExNext` → `FileInfoBlock`. *Done when:* a program locks the
  root, ExNext-walks it, and sees Clar's drawer entry.
- **L3.4 — file I/O.** `Open`/`Close`/`Read`/`Write`/`Seek` over the
  CaraFS file ops; `FileHandle`. *Done when:* create/write/seek/read a
  file back, contents match.
- **L3.5 — mutation + info.** `CreateDir`/`DeleteFile`/`Rename`/
  `SetProtection`/`SetComment`/`SetFileDate`; `Info`.
- **L3.6 — process/CLI + console handler.** `Output`/`Input`,
  console-handler `Write`/`Read` (stdout/stdin), `Delay`. (`IoErr` from
  L3.1.)
- **L3.7 — retire the stopgap.** Delete the app-facing `SYS_Fs_Read/
  Write` name-in-root path; repoint **Clar** to dos
  `Open`/`Read`/`Write`/`Close`; keep the handler-only kernel FS
  syscalls. Update the boot smoke so Clar's drawer file persists *via
  dos*. *Done when:* the Phase-2 criterion (a file edited in Clar's
  drawer survives reboot) holds with `Croi_Fs_*` gone from the app path.

---

## 6. Open questions

1. **Reply-port lifetime vs. the Process move.** The per-task reply port
   (L3.2) and `pr_MsgPort` both want to be created at task startup in
   libcara. Settle whether `pr_MsgPort` *is* the reply port or a second
   port (AmigaDOS Processes have a dedicated `pr_MsgPort` for packets).
2. **Handler concurrency.** v0 is one handler Gleas, synchronous. Two
   Gleasanna both doing FS will serialise on its MsgPort — fine for
   correctness, revisit if it bottlenecks.
3. **Console handler shape.** Minimal stdout (log-backed) vs. a real
   console window via intuition (needs L5). v0: log-backed stdout +
   stub stdin; upgrade after L5.
4. **`Delay` / `WaitForChar` timing.** Needs a timer signal source;
   may pull a small `timer.device`-shaped dependency forward from L6, or
   a `syscall` shim over `Croi_Time` for v0.
5. **Seglist / `LoadSeg`.** Out of scope for the apps (they're spawned
   from embedded ELFs, not AmigaDOS overlays); keep stubbed.
