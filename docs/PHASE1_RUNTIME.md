# Phase 1 runtime plan — Epics and Stories

> Working plan for the rest of Phase 1 Subgoals 2 (Croi runtime) and 3
> (console). Pairs with `docs/ROADMAP.md` (which scopes Phase 1) and
> `docs/ARCHITECTURE.md` (which defines the contracts every Story below
> implements). Updated as Stories land.
>
> **Naming note.** Every `Croi_*`, `Dath_*`, `Page_*`, `Ring_*` symbol
> in this document is **kernel-internal**, in the brand namespace
> defined by `docs/PRINCIPLES.md` §3.1. The Phase 1 contracts here
> are the *implementations* the V36+ exec.library LVOs trampoline
> into — `AllocMem`, `Wait`, `Signal`, `PutMsg`, `GetMsg`,
> `WaitPort`, `AllocSignal`, `FreeSignal`, … — and they are free to
> use the brand namespace because they're not themselves the API
> user programs see. The exec.library trampolines, the generated
> `<proto/exec.h>` inline stubs, and the user-VA library mapping at
> `0x4000_0000` shipped as Phase A–E (see `docs/LVO.md` and below);
> Phase 3 proper continues with `dos.library`, `intuition.library`,
> and the rest of the V36+ surface.

---

## Status — 2026-05-08

Tier 1 (console-output-complete): **shipped**.
Tier 2 (general runtime): E, F, G, I, J **shipped**; H deferred.
Tier 3 (userspace prep): K **shipped** (full — embedded asm-section
spawn AND parsed-ELF spawn from a separately-compiled cross binary).

Phase 1 Subgoal 4 (framebuffer / `graphics.library` foundation under
the brand-namespace **Dath** module): **first cut
shipped** — DathFramebuffer + draw primitives + simple-framebuffer
FDT discovery + 8x8 bitmap font + DrawChar/DrawString. QEMU virt has
no framebuffer node by default so the boot logs "headless boot"; the
synthetic in-heap framebuffer tests verify all primitives end-to-end.

**Phase 3 foundation (exec.library) shipped** — out of Phase 1's
scope strictly, but landed on top of the Phase 1 substrate:

  - `tools/lvo-gen` host tool (parser + validator + four emitters
    + Phase 9 aggregator), CMake helper, hosted golden-file tests.
  - V36+ namespace clearance: `<exec/{types,nodes,lists,ports,
    libraries,memory,tasks,execbase}.h>`, `<utility/tagitem.h>`,
    brand squatters renamed (CroiMsgPort, evolved struct Task with
    tc_* fields, MinList_* helpers).
  - exec.library substrate: 13 syscalls (OpenLibrary / CloseLibrary
    / AllocMem / FreeMem / Wait / Signal / AllocSignal / FreeSignal
    / SetSignal / PutMsg / GetMsg / WaitPort / OldOpenLibrary),
    Croi_MakeLibrary + library registry, per-LVO syscall trampolines,
    linker-placed library image at user-VA 0x4000_0000 (R+W+X+U,
    installed in every PT including the boot PT), boot-time
    construction.
  - libcara C runtime: bootstrap-opens exec.library via inline
    ecall, sets the SysBase global, runs main, calls SYS_EXIT.
  - LVO.md §9 worked example: `userexec.elf` reads
    `SysBase->LibNode.lib_Version` (a plain memory load — proof
    the SASOS user-VA mapping is live), then exercises OpenLibrary
    / AllocMem / FreeMem / CloseLibrary through the generated
    `<proto/exec.h>` inline stubs.

See `docs/LVO.md` for the design and the Phase A–E commits
(`efefe4c phase-A+B`, `3078c5e phase-C kernel`, `b67fefd phase-C
end-to-end`, `6ab4a1c phase-D`, `0ec0b61 phase-E`) for the
implementation.

Thirteen in-kernel tests run on every boot; the QEMU smoke ctest
asserts `kernel tests: N passed, 0 failed`:

  pagealloc_smoke, heap_smoke, time_smoke, sched_smoke,
  signal_smoke, handle_smoke, ipc_smoke, paging_smoke,
  usermode_smoke, userelf_smoke, dath_smoke,
  exec_lib_smoke, userexec_smoke

What landed under each Epic:

  - **A.1/A.2** trap entry + frame + dispatcher with decoded panics
  - **A.4** Sstc S-mode timer (Croi_Time_Now / SetDeadline)
  - **B.1** PhysMap from FDT (memory + reserved + kernel + DTB carve-outs)
  - **B.2** bitmap page allocator (per-run bitmaps in-place)
  - **B.3** size-class kernel heap with page-magic free dispatch
  - **C.1–C.3+C.5** structured logging — LogRecord, ring, sinks, banner
  - **C.4** ANSI level coloring on the UART sink
  - **D.1–D.3** kernel self-test harness via .kernel_tests linker section
  - **E** cooperative scheduler (priority + round-robin within), kmain
    bound to boot context, kernel-thread Spawn API, dead-task reaping
  - **F** Exec-style signals (sigrecvd/sigwait/sigalloc) + sleeper queue
  - **G** Kobj base (atomic refcount, monotonic id, destroy callback) +
    per-task HandleTable with 16-bit generation validation
  - **I** MsgPort wrapping Ring + Signal — full producer/consumer IPC
  - **J** Sv39 page-table walker + Page_Map (first cut: kernel-only PTs,
    no per-task satp save)
  - **K** U-mode tasks + ecall syscall dispatch — sscratch swap
    convention in trap entry, user_task_trampoline switches satp +
    sret's into U-mode at CARA_USER_TEXT_BASE = 0x10000, SYS_LOG_WRITE
    / SYS_EXIT handlers read user pointers via SUM=1, two spawn paths:
      * Croi_SpawnUserTask — maps an embedded .user_text section
        (asm program reaches SYS_EXIT 42)
      * Croi_SpawnUserTaskFromElf — parses static ELF64 PT_LOAD
        entries from src/userland/userhello.elf cross-compiled by
        clang and embedded via .incbin (C program reaches SYS_EXIT 1234)
  - **dath (Phase 1 Subgoal 4)** — DathFramebuffer abstraction
    (RGBA8888 / BGRA8888 / RGB565 formats), simple-framebuffer FDT
    discovery (Dath_Framebuffer_FromFdt → DathFbDescriptor), and a
    full drawing surface:
      * Dath_Pixel / Dath_FillRect / Dath_Clear / Dath_BlitRect with
        complete clipping (negative origins, oversize spans, partial
        off-screen rects) — RGBA8888 + BGRA8888 path.
      * Dath_DrawLine (Bresenham, all eight octants) and Dath_DrawRect
        (outlined rectangle, four edges via DrawLine).
      * Dath_DrawChar / Dath_DrawString over an 8x8 bitmap font that
        covers space, '!', ',-./:' digits 0-9, uppercase A-Z, lowercase
        a-z. Unmapped slots render blank, never garbage.
      * DathConsole wraps a framebuffer + font into a cursor-tracked
        text surface with newline / carriage-return / soft-tab handling
        and auto-scroll-up via Dath_BlitRect. Log_Sink_DathConsole_Emit
        registers as a regular LogSink, so once a framebuffer is up,
        every Croi_Log line at INFO+ renders to screen alongside the
        UART.
      * Boot path probes the FDT for simple-framebuffer; on success
        clears the framebuffer dark blue, draws a 96x96 lighter-blue
        boot pattern, and registers the FB log sink. Headless QEMU
        virt logs "no simple-framebuffer in FDT" and continues normally.
      * cara_dath is dual-target so a host unit test (test_dath_fdt)
        verifies the simple-framebuffer FDT parser without a kernel.

Deferred until they're actually needed:

  - **A.3** per-hart trap stack — single-hart Tier 2 runs on the
    kernel stack; arrives with Epic H
  - **H** SMP secondary harts — needs per-hart current pointer +
    run-queue locking that ripples through every existing module;
    the cooperative single-hart cut already validates Tier 2 contracts
  - **Per-task satp save in ctx_switch + ASID rotation** — Epic K's
    first cut works because the user task only context-switches via
    SYS_EXIT (which doesn't return to the user PT). When Phase 3
    introduces yield-from-syscall, ctx_switch needs to track satp too.
  - **Timer-driven preemption** — the cooperative path is good enough
    while we're still S-mode-only; preempt handling needs the trap
    frame to be the canonical "saved context" format which is a small
    refactor of Epic E

---

## Context

Slice 1 is committed. Croi boots under `qemu-system-riscv64 -M virt
-kernel`, OpenSBI hands off in S-mode, our pre-MMU `_start` builds an
Sv39 boot page table, paging is on, the kernel runs at upper-half VAs
(0xFFFFFFC080204000+), the FDT parser identifies the NS16550 console,
and `Croi_Log` writes through it. `ctest` is green: 6 host unit tests
plus a QEMU boot smoke that asserts both hello strings.

**This plan is what comes next.**

### Strategy

**Console-output-complete first.** The console is the only diagnostic
surface we have — and the only one we'll have until USB-HID lands much
later. Every later Story logs *through* the console, so getting the
console feature-complete now compounds: each subsequent capability
lands with structured logs and an in-kernel test that prints a verdict.

Concretely that means Tier 1 below ships before any Tier 2 work
starts. Tier 2 then builds out the general runtime (tasks, signals,
IPC, per-task paging) with each Epic adding tests to the harness Tier 1
established.

### Out of scope for this plan

- **All input.** No UART RX, no kernel REPL, no line discipline. Input
  arrives with the USB stack much later. The console is *write-only*.
- **virtio anything.** No virtio-blk, no virtio-net, no virtio-keyboard.
  When storage and networking land they go through the real silicon
  paths (NVMe in Phase 2, the X1 MAC in Phase 5).
- **Splanc / UEFI.** QEMU `-kernel` boot is the daily driver; the UEFI
  bootloader is its own slice when we move to real OrangePi RV2 silicon.
- **Phase 1 Subgoals 4–7** (framebuffer, USB host, Leargas, Clar). They
  get their own plans once Tier 3 here completes.

### Reading guide

- Each **Epic** has a one-line goal, dependencies, and an exit criterion.
- Each **Story** is one focused PR's worth of work: scope (what lands),
  verification (how we know it works), files (where).
- Order within a Tier is the suggested landing order. Cross-Tier
  reordering is fine when it doesn't break a dependency.

---

# Tier 1 — Console-output-complete

After Tier 1, the kernel writes structured, timestamped, level-tagged
log records to the NS16550 console *and* a wraparound in-memory log
ring; an in-kernel test harness runs at boot, prints `PASS`/`FAIL` per
test, and `ctest` greps the QEMU log for a single magic string. Every
later Epic plugs into this without changing the console abstraction.

## Epic A — Trap & exception infrastructure

**Goal:** Croi survives any S-mode trap (synchronous exception, timer,
external IRQ) and prints a usable diagnosis on the ones it can't
service.

**Why now:** Tier 1's later Epics need timer ticks for timestamps,
later Tiers need IRQs to do anything async, and right now any fault
silently halts the CPU.

**Exit:** synthetic `unimp` and bad-load tests fault to a structured
panic with scause/sepc/stval decoded; a 100 ms one-shot timer arrives
within ±5% of its deadline.

### A.1 — TrapFrame + asm save/restore

Define `struct TrapFrame { u64 x[32]; u64 sepc, scause, stval, sstatus,
sscratch_saved; };`. Add `src/croi/trap_entry.S` that swaps sp via
sscratch, builds the frame on a per-hart trap stack, calls
`Croi_TrapDispatch(&frame)`, restores, sret. Reserve x0 slot for clean
indexing. Verify by reading back any single saved register from a
contrived trap.

### A.2 — Exception classifier + early-panic

`src/croi/trap.c`: `Croi_TrapDispatch` switches on scause. All
synchronous exceptions print decoded reason + frame summary via
`Croi_Log` and panic. Asynchronous bits forward to (still-empty) IRQ
and timer paths. Verify: deliberate `unimp` and a load from
`0xdeadbeef` produce clean panics with the right scause codes.

### A.3 — Per-hart trap stack

Allocate one 16 KiB trap stack per hart (one for now). sscratch holds
its top. Trap entry swaps to it; trap exit restores caller sp. The
boot stack stays for the foreground task. Verify: take a trap inside
a deep callstack, observe sp restored exactly.

### A.4 — Sstc S-mode timer

Detect Sstc presence via FDT `riscv,isa`. `Croi_Time_Now()` reads
`time` CSR, scales by `cpus/timebase-frequency` from the FDT to ns.
`Croi_Time_SetDeadline(u64 ns)` writes stimecmp + sets sie.STIE.
Timer trap clears its enable, optionally re-arms. Verify: schedule
100 ms, log arrival, repeat — ±5% tolerance.

---

## Epic B — Physical memory + kernel heap

**Goal:** any kernel module allocates page-aligned and small-object
memory without colliding with the boot image, the FDT, or reserved
regions.

**Depends on:** Epic A only for trap-safe assertions.

**Exit:** stress test allocates and frees 64 MiB worth of pages and
1 M heap objects across mixed size classes; counters return to baseline;
no fault.

### B.1 — Physical memory map from FDT

Walk `/memory@*` (multi-bank aware — X1 has two banks). Walk
`/reserved-memory/*`. Subtract the kernel image phys range (linker-
emitted `_kernel_phys_start`/`_kernel_phys_end`) and the DTB phys
range. Result: `struct PhysMap` of usable runs. Log the map at boot.

Files: `src/croi/mm/physmap.c`, `include/cara/mm.h`.

### B.2 — Bitmap page allocator

4 KiB pages, one bit per page, allocator state in the largest usable
run. `Page_Alloc(n)` / `Page_Free(phys, n)`. Track in-flight count
and high-water mark. Allocations zeroed before return.

Files: `src/croi/mm/page_alloc.c`.

### B.3 — Kernel heap (size-class freelists)

Size classes 16 / 32 / 64 / 128 / 256 / 512 / 1024 / 2048 bytes;
larger requests go straight to the page allocator. `Croi_Alloc(size)`
/ `Croi_Free(ptr)`. Per-class freelist + slab-of-pages backing.
Counters: bytes in-flight per class, peak. No coalescing in v0.

Files: `src/croi/mm/heap.c`, `include/cara/alloc.h`.

---

## Epic C — Console output stack

**Goal:** every byte of output goes through a structured log path with
timestamp, hartid, level, source tag, and message; output fans out to
multiple sinks (NS16550, log ring, future framebuffer text mode); ANSI
escapes pass through to capable sinks and are stripped from the
others.

**Depends on:** Epic A (timestamp via `Croi_Time_Now`), Epic B (ring
buffer storage).

**Exit:** boot output reads
```
[t=0.000123] [I] [boot] CaraOS Croi v0.0.1 (build abcdef)
[t=0.000456] [I] [fdt ] memory: 0x80000000..0x88000000 (128 MiB)
[t=0.000789] [I] [uart] NS16550 base=0x10000000 baud=115200
```
The same lines are visible by replaying the in-memory log ring.
Stripping a sink's ANSI capability changes the byte stream but not
the message stream.

### C.1 — Log record format + ring buffer

```c
struct LogRecord {
    u64 ts_ns;
    u8  level;        // TRACE/DEBUG/INFO/WARN/ERROR/FATAL
    u8  hartid;
    u16 _pad;
    char tag[8];
    char msg[120];
};
```
64 KiB ring (~512 records); oldest evicted on overflow. Append
operation is wait-free single-producer (one hart at a time during
Tier 1; SMP locking comes in Epic H). Replay walks oldest-to-newest.

Files: `src/croi/log/ring.c`, `include/cara/log.h`.

### C.2 — `Croi_Log` API + level macros

`Croi_Log(level, tag, fmt, ...)` formats into a `LogRecord`, appends to
the ring, fans out to active sinks. Convenience macros:
```c
LOG_TRACE("fdt", "node @%u name=%s", off, name);
LOG_INFO("boot", "Hello from Croi");
LOG_WARN("uart", "FIFO threshold not configurable on this rev");
LOG_ERROR("mm",  "page alloc exhausted run %u", run);
```
Compile-time min-level filter (drops macros entirely below threshold).
Runtime min-level filter for fine tuning. `Croi_Log` does not allocate.

### C.3 — Sink abstraction + migration

```c
struct LogSink {
    void (*emit)(const struct LogRecord *r, void *ctx);
    void *ctx;
    bool ansi_capable;
    u8   min_level;
};
```
A small static array of sinks (max 4 for now). Default sinks: SBI
DBCN (early), NS16550 (after FDT), log ring (always). `Croi_Print` is
demoted to the SBI-only path used before any sinks are registered.
All other callsites move to `Croi_Log`.

Files: `src/croi/log/log.c`, `src/croi/log/sink_sbi.c`,
`src/croi/log/sink_ns16550.c`.

### C.4 — ANSI-passthrough emitter + minimum subset

Sinks declare `ansi_capable`; non-capable sinks strip CSI sequences
on emit. Helpers in `cara/log.h`: `LOG_C_RED`, `LOG_C_GREEN`,
`LOG_C_RESET` macros that expand to escape strings and are pre-stripped
in non-capable paths. Used by the test harness for `PASS`/`FAIL`
coloring. Verify: capture the same boot under ANSI-on and ANSI-off,
diff to confirm semantically equivalent text.

### C.5 — Boot banner + system summary

On entry into `croi_entry`: print kernel version, build commit, hartid,
memory bank summary, FDT root model, console UART parameters, Sstc
present/absent. One-line each, INFO level. Visible immediately on boot
— the primary debuggability win for Tier 1.

Files: `src/croi/boot_banner.c`.

---

## Epic D — Kernel self-test harness

**Goal:** every later Epic ships an in-kernel boot test that prints
PASS/FAIL; `ctest` greps for `kernel tests: N passed, 0 failed` and
fails the build on any regression.

**Depends on:** Epic C (so PASS/FAIL goes through structured logging).

**Exit:** tests for heap, page allocator, and timer all pass; the
QEMU smoke ctest greps for the success line; deliberately failing one
test causes a red ctest failure pinpointing the test name.

### D.1 — Test runner core

```c
typedef void (*KernelTestFn)(struct TestCtx *ctx);
struct KernelTestEntry { const char *name; KernelTestFn fn; };

#define KERNEL_TEST(NAME)                                                \
    static void test_##NAME(struct TestCtx *);                           \
    static const struct KernelTestEntry _ktest_##NAME                    \
        __attribute__((used, section(".kernel_tests"))) =                \
        { #NAME, test_##NAME };                                          \
    static void test_##NAME(struct TestCtx *ctx)
```
Linker exposes `__kernel_tests_start` / `__kernel_tests_end` (add to
`croi.lds`). Boot path runs each entry sequentially, prints
`[TEST] name … PASS` (green) or `[TEST] name … FAIL: <reason>` (red),
aggregates totals.

Files: `src/croi/test/runner.c`, `include/cara/test.h`, +`croi.lds`
section emit.

### D.2 — Initial tests

- `T_heap_smoke` — alloc 1024 objects across all size classes, free,
  assert in-flight count back to 0 and peaks recorded.
- `T_pagealloc_smoke` — alloc and free 1000 pages in random order,
  assert freelist consistency.
- `T_time_smoke` — schedule 100 ms timer, wait, assert elapsed within
  ±5%; repeat 5 times.

### D.3 — ctest wiring

`tests/boot/smoke_qemu_kernel.sh` adds an assertion that
`kernel tests:` line ends in `0 failed`. Failures echo the `[TEST]`
lines from the captured log so the failing test name surfaces in
ctest output.

---

**Tier 1 exit:** at this point Croi prints a structured boot banner,
runs an in-kernel test suite, asserts results, and `ctest` enforces
green on every build. Every later Epic adds tests; the console itself
is feature-complete for the no-input phase. Stop here, ship, then
move to Tier 2.

---

# Tier 2 — General runtime (output-only)

Tier 2 builds out the rest of Croi: tasks, signals, kernel objects,
SMP, IPC, per-task paging. Each Epic adds tests to the harness from
Epic D. Userspace doesn't exist yet, so this is all S-mode.

## Epic E — Tasks & scheduler

**Goal:** multiple kernel-mode threads round-robin scheduled on hart 0,
clean idle path, kernel-thread spawn API.

**Depends on:** Epics A (trap), B (heap for stacks).

**Exit:** four spawned tasks log 100 messages each in interleaved
priority order; idle hart enters WFI when no task runnable.

- **E.1** Task struct (per ARCHITECTURE.md §5.3) + per-hart `current`.
- **E.2** Context-switch asm: save callee-saved + sscratch + satp;
  reload from incoming task.
- **E.3** Round-robin scheduler with run / sleep queues using
  `cara/list.h` intrusive lists.
- **E.4** Idle task: WFI loop; sets sstatus.SIE so timer wakes it.
- **E.5** `Croi_SpawnKernelTask(name, pri, entry, arg)`.
- **E.6** `T_sched_smoke`: 4 tasks logging interleaved with
  prio-respect ordering.

## Epic F — Signals

**Goal:** Exec-style 32-bit signal masks for inter-task wakeups.

**Depends on:** Epic E.

**Exit:** producer task `Signal()`s a bit; consumer parked in `Wait()`
wakes immediately; `T_signal_smoke` asserts no lost wakeups across 10k
iterations.

- **F.1** Per-task `sigrecvd` / `sigwait` masks; inline ops on the
  Task struct.
- **F.2** `Wait(mask)` / `Signal(task, mask)` / `SetSignal` / `AllocSignal`.
- **F.3** `KOBJ_SIGNAL` for cross-task addressable signals.
- **F.4** `T_signal_smoke`.

## Epic G — Kobj + Handle table

**Goal:** every kernel resource is a typed, refcounted Kobj exposed to
holders via Handles with type and generation checks.

**Depends on:** Epic B.

**Exit:** open 1000 handles, close half, exhaust generation counter on
one slot, assert stale-handle access returns `CARA_EBADF` rather than
faulting.

- **G.1** Kobj base (`type`, `flags`, atomic refcount, monotonic id).
- **G.2** Per-task `HandleTable` allocator with free-list.
- **G.3** `Croi_HandleOpen` / `Close` / `Dup`; type-checked lookup.
- **G.4** `T_handle_smoke`.

## Epic H — SMP: secondary harts online

**Goal:** all harts running the scheduler, sharing the run queue, taking
their own timer interrupts.

**Depends on:** Epics A, E.

**Exit:** per-hart counter task on each hart logs N messages; total
count == N × hart_count; under contention, no missed scheduler ticks.

- **H.1** SBI HSM `hart_start` for harts 1..N; secondary entry sets
  per-hart sscratch + idle task and joins the scheduler.
- **H.2** Per-hart trap stack and run queue (or shared with cache-line-
  conscious locking).
- **H.3** Simple work-stealing: idle hart pulls from another hart's
  queue.
- **H.4** Adapt `Croi_Log` ring append for multi-producer: per-record
  sequence number + atomic head; consumer (replay) tolerates torn
  writes via a checksum byte.
- **H.5** `T_smp_smoke`.

## Epic I — IPC: Ring runtime + MsgPort

**Goal:** typed messages flow between tasks via SPSC ring buffers
wrapped as MsgPorts, exactly the contract in `cara/ring.h` and
ARCHITECTURE.md §6.

**Depends on:** Epics F, G.

**Exit:** four producer tasks send 10 000 messages each into one
consumer's MsgPort; consumer logs receipt totals; no message lost,
ordering per-producer preserved.

- **I.1** Ring backing-store allocator (heap-backed for v0; SASOS
  IPC pool comes with per-task paging in Epic J).
- **I.2** `KOBJ_RING` wrapping `RingHeader` + slot array.
- **I.3** `KOBJ_MSGPORT` = ring + signal kobj (cross-references Epic F).
- **I.4** `Croi_PutMsg` / `Croi_GetMsg` / `Croi_WaitPort` per
  ARCHITECTURE.md §6.6.
- **I.5** `T_ipc_smoke`.

## Epic J — Per-task paging + ASID

**Goal:** every Task has its own Sv39 root with the kernel upper half
shared and the lower half private; ASID rotation reuses TLB entries
without flushing.

**Depends on:** Epics B, E, G.

**Exit:** two tasks running in S-mode each map a distinct lower-half
page to different bytes; reading task A from task B faults; reading
the kernel direct map is identical in both.

- **J.1** `Croi_NewTaskPageTable()` clones the kernel upper half
  (currently L2[256] / [258]; later: per-region) and zeros lower half.
- **J.2** `Page_Map(pt, va, pa, prot)` / `Page_Unmap`. Walk-or-allocate
  intermediate tables.
- **J.3** ASID allocator (16-bit, per-hart current + global generation).
- **J.4** Context-switch path writes new satp with `(asid << 44) | ppn`
  and skips sfence when ASID changes alone (TLB stays valid).
- **J.5** `T_paging_smoke`.

---

# Tier 3 — Userspace prep (still output-only)

A minimum bridge to U-mode: load a static ELF, switch to U-mode,
service a single syscall (write to log). Establishes the syscall and
LVO patterns that everything in Phase 3 (AmigaOS API parity) builds on.

## Epic K — ELF loader + first U-mode task

**Goal:** an embedded user ELF runs in U-mode, calls one syscall, and
returns. Syscall path is the canonical `ecall` from U → S used by every
later library.

**Depends on:** Tier 2 in full.

**Exit:** an embedded `userhello.elf` prints `hello from U-mode` via a
`SYS_log_write` syscall and exits cleanly; `T_userhello_smoke` confirms
the line appears in the kernel log ring.

- **K.1** `tools/blob_wrap/` host tool (or asm `.incbin`-style stub à la
  VectraOS) that embeds a user ELF into croi.elf as a named section.
- **K.2** ELF64 loader for static, no-relocation user binaries:
  walks `PT_LOAD`, allocates pages, maps into the task's lower half.
- **K.3** U-mode task spawn: reuses Epic E machinery; sets sstatus.SPP
  = 0; sret to `e_entry`.
- **K.4** Syscall ABI: `ecall` from U-mode traps to S-mode; `a7` =
  syscall number; `a0..a5` = args; return in `a0`. Numbers are
  `constexpr int` in `include/cara/sysno.h`.
- **K.5** `SYS_log_write(level, tag, ptr, len)` — copies into a Kobj
  buffer, appends `LogRecord`. SUM bit setup so S-mode can read U-mode
  pages (per ARCHITECTURE.md §3 / VectraOS pattern).
- **K.6** `userhello.c` linked into a tiny static ELF. Embedded.
  `T_userhello_smoke` runs it on boot and asserts the magic line.

---

# What follows

After Tier 3 landed, two strands run in parallel:

**Phase 1 Subgoals 4–7** — extend Phase 1's runtime substrate.
All four plan docs now exist:

- **`docs/PHASE1_FRAMEBUFFER.md`** *(Subgoal 4 — first cut shipped)*
  — `simple-framebuffer` discovery from FDT, CPU blitter, the Dath
  rasteriser Clar will draw through.
- **`docs/PHASE1_USB.md`** *(Subgoal 5 — not started)* — cleanroom
  xHCI driver running unchanged on QEMU's `qemu-xhci` and the
  X1's onboard controller; PCIe enumeration and USB device
  enumeration; HID boot-protocol class driver as a Gleas. No
  virtio shortcut.
- **`docs/PHASE1_LEARGAS.md`** *(Subgoal 6 — not started)* —
  pointer, screen, window, focus, keyboard routing, string gadget.
  Phase 1 minimum substrate; Phase 3's `intuition.library` wraps
  this with the V36+ LVO surface.
- **`docs/PHASE1_CLAR.md`** *(Subgoal 7 — not started)* —
  Workbench analogue: backdrop screen, one Bosca (drawer), one
  Inntin (text-input gadget). When this works under QEMU with
  `qemu-xhci` + `usb-kbd` + `usb-mouse`, Phase 1's QEMU
  equivalent ships.

**Phase 3 (V36+ AmigaOS API parity)** — kicked off with exec.library
per the Status section above. Subsequent libraries each land via the
same lvo-gen pipeline and don't need a per-library plan document; the
shared design is `docs/LVO.md`. Roadmap order is roughly utility →
intuition → graphics → dos → gadtools → asl → iffparse → the rest of
ROADMAP.md §Phase 3. Subgoal 6 (Leargas) and Phase 3's
`intuition.library` are the same milestone wearing two hats — Leargas
is the brand-namespace impl, intuition.library is its V36+ public
face.
