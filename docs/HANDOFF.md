# CaraOS session handoff — 2026-06-11

> A pick-up-where-we-left-off note for a fresh session. Captures
> current state, the non-obvious decisions from the last sprint, the
> concrete next steps, and the build/test/commit workflow. Pairs with
> `docs/ROADMAP.md` (the phase plan), `docs/CARAFS.md` (the filesystem
> design — the current work), and `docs/ARCHITECTURE.md` (the system
> design).

---

## 1. Where we are

**Phase 1 shipped** (see §5 for the live demo recipe). **Phase 2 is
complete under QEMU** (all four subgoals; criterion met at `fe701fb`,
Subgoal-3 finished by G4+G2): Clar edits a file in its drawer and the
change persists across reboot, on a CaraFS volume mounted from an
NVMe-resident GPT partition, with `S/Startup-Sequence` run at boot. The
on-disk format is frozen (F4). ROADMAP reserves a `STATUS: complete`
line for the same demo on real hardware (no RV2 board yet). **Phase 3 is
underway** — planned in `docs/PHASE3.md`, **P0 (ABI/toolchain proof)
shipped**; **L1 (widen `exec.library`) is next**.

Recent commits (newest first), all on `main`:

```
95a593d phase-3/L4.7  live Screen.RastPort over the framebuffer (L4 complete)
ea39d2a phase-3/L4.6  graphics.library area fill (InitArea/AreaMove/AreaDraw/AreaEnd)
c9f6492 phase-3/L4.5  graphics.library text + fonts (OpenFont/SetFont/Text/TextLength)
7d51b32 phase-3/L4.4  graphics.library blits (BltBitMap/BltBitMapRastPort/ClipBlit)
15db462 phase-3/L4.3  graphics.library pen state + primitives (Move/Draw/RectFill/...)
ce1c41b phase-3/L4.2  graphics.library BitMap alloc + RastPort init + SetRast
8eff42f phase-3/L4.1  graphics.library ABI + GfxBase + boot construction
d10983b phase-3/L3.7  retire Croi_Fs_* stopgap, Clar uses dos.library (closes L3)
7db275d phase-3/L3.6  dos.library process I/O + console + Delay (Output/Input/Delay)
cf310c9 phase-3/L3.5  dos.library mutation + info (CreateDir/DeleteFile/Rename/Info)
7c0584e phase-3/L3.4  dos.library file I/O (Open/Close/Read/Write/Seek)
1b64e8e lvo-gen        disambiguate reserved-slot identifiers (LIB_OPEN etc.)
3725f1a phase-3/L3.3b dos.library Examine / ExNext
3af2c52 phase-3/L3.3  dos.library locks (Lock/UnLock/DupLock/CurrentDir)
ba08b31 phase-3/L3.2  dos handler server task + U-mode packet round-trip
014ca2b phase-3/L3.1  dos.library ABI foundation + Process model
e52d812 phase-3/L2    utility.library allocating tag helpers (slice 3)
5870c8b phase-3/L2    utility.library MapTags/FilterTagItems/FilterTagChanges (slice 2)
6a4b462 phase-3/L2    utility.library tag walkers + Hook (slice 1)
92e256d phase-3/L1    lvo-gen --coverage stub-coverage report (closes L1)
2dc5e58 phase-3/L1    exec.library Forbid/Permit/Disable/Enable (slice 6)
af24dde phase-3/L1    exec.library signal semaphores (slice 5)
7c173b3 phase-3/L1    exec.library FindTask + CopyMem/CopyMemQuick (slice 4)
1763f81 phase-3/L1    exec.library CreateMsgPort/DeleteMsgPort/ReplyMsg (slice 3)
5d0dce6 phase-3/L1    exec.library AllocVec / FreeVec (slice 2)
92b4557 phase-3/L1    exec.library list primitives (slice 1, first local LVOs)
9c8429b phase-3/P0    verbatim-V36 source builds + runs (ABI proof)
aee7d29 docs          Phase 3 plan (docs/PHASE3.md)
c2f947f docs          Phase 2 complete under QEMU
8c25ad7 phase-2/F6 G2 UUID-aware root selection + multi-partition GPT
3c3c279 phase-2/F6 G4 S/Startup-Sequence runner at boot
fe701fb phase-2/F6 G3 Clar edits a CaraFS file — Phase 2 criterion met
b7c80aa phase-2/F6 G1 GPT partition discovery + partition-relative mount
0da9d95 phase-2/F5    CaraFS kernel mount over NVMe + reboot persistence
d941883 phase-2/F4    CaraFS journal — WAL, ordered data, replay, crash test
0af45f8 phase-2/F3    CaraFS directories — inline→leaf→tree, links, scale
591d1d3 phase-2/F2    CaraFS cnodes, files, allocator
a286aa0 phase-2/F1    CaraFS bdev + cache + mkfs/fsck v0
16b5265 phase-2/F0    CaraFS format foundation
```

Status: everything green — host ctest **27/27**, in-kernel tests
**32 passed / 0 failed** (L4.7 added `graphics_screen_rastport`), QEMU
boot smoke ok (two boots: partition +
format + startup-sequence + Clar drawer file persists; plus the P0
`v36hello_smoke`), `format-check` clean. (Host suite ~20–25 s.)

### Phase 2 so far

- **NVMe (Subgoal 1, `5cb4c0c`)**: `include/cara/nvme.h` +
  `src/croi/nvme/` — probe → admin queues → Identify → polled I/O
  queue pair (QID 1) → `Croi_Nvme_Read/Write` (PRP1+PRP2, ≤ 8 KiB per
  command). Cleanroom from NVMe Base Spec 1.4. Parked follow-ons
  (PRP lists, MSI-X, `Croi_Nvme_Flush`, nvme.device) are listed in
  `docs/PHASE2_NVME.md`. The boot smoke harness attaches a throwaway
  16 MiB NVMe image; booting *without* it fails the 3 nvme
  KERNEL_TESTs (expected, same caveat as missing usb-kbd/usb-mouse).
- **CaraFS design (`b0463ae`, `docs/CARAFS.md`)**: metadata journaling
  over CoW (decision recorded in §3.9); cnode-id == block-number; TLV
  cnode items; FNV-1a-64 folded-name hash; shared B+tree for dirs +
  extents; AG bitmaps; circular WAL with ordered data. Epic plan in
  §6: F0 format → F1 mkfs/fsck → F2 files → F3 dirs → F4 journal
  (format freezes after F4) → F5 kernel mount → F6 boot path.
- **CaraFS F0–F2**: see §2. The core lives in `src/logaic/carafs/`
  behind the `CarafsBdev` seam (host file / mem bdev now, kernel NVMe
  binding in F5), with hosted `mkfs.carafs` / `fsck.carafs` tools in
  `tools/carafs/` and host unit tests driving everything.

---

## 2. CaraFS: what exists and the internals you need (F0–F2)

All on-disk structs live in `include/cara/carafs.h` — the
authoritative byte layout, every offset static_asserted, LE-host
compile gate. Public API so far: `Carafs_Mkfs`, `Carafs_Fsck`,
`Carafs_Mount/Unmount/Sync`, `Carafs_CnodeCreate/Delete/Stat`,
`Carafs_FileRead/Write`.

Per-file map of `src/logaic/carafs/`:

- `format.c` — CRC-32C, block-crc (field-zeroed convention), ASCII
  fold, FNV-1a-64 name hash, name validation.
- `bdev.c` — memory-backed bdev (the unit-test workhorse).
- `cache.c` — fixed-capacity write-back block cache over the
  caller-supplied arena (LRU + bucket index + pinning) and the
  transaction bracket (`carafs_txn_begin/dirty/commit/abort`).
- `mount.c` — mount/unmount/sync, superblock validation, CLEAN↔DIRTY
  state machine, and the **per-operation bracket**
  `carafs_op_begin/commit/abort` (mark-dirty + txn + sb snapshot).
- `alloc.c` — AG bitmap allocator: first free run from the per-AG
  rotor, round-robin AG fallback, exact per-AG free counts.
- `cnode.c` — cnode get/put/dirty (verify on cache-miss), create /
  delete / stat, TLV item area (find/resize/remove, 8-byte stride).
- `btree.c` — **extent-flavour** B+tree: floor/next, insert with
  predecessor merge + splits + root growth, spill-from-inline,
  post-order free-all.
- `file.c` — read/write with the storage escalation INLINE_DATA item
  → 16 inline extents → extent tree; sparse holes.
- `mkfs.c` / `fsck.c` — as of F1; fsck cross-checks AG free counts
  vs popcounts and the advisory sb total on clean volumes.

**Non-obvious internals (read before touching the core):**

1. **The op bracket.** Every public mutator is exactly one
   `carafs_op_begin → mutate → carafs_op_commit` (abort on any error).
   begin = writability check + durable CLEAN→DIRTY flip + txn_begin +
   in-memory superblock snapshot; commit = `carafs_sb_write` (block 0
   joins the txn) + txn_commit; abort = txn_abort + sb restore.
   Pre-F4, txn_commit writes the txn's blocks home and flushes — F4
   replaces that body with a WAL append.
2. **Metadata vs data in the cache.** Metadata blocks join the open
   txn via `carafs_txn_dirty` (TXN entries are never evicted; abort
   drops them wholesale). File **data** blocks are plain dirty
   entries (`carafs_cache_dirty`) written back on eviction, sync, or
   unmount — they never consume txn capacity
   (`CARAFS_TXN_MAX_BLOCKS` is 64). F4's ordered-data discipline will
   sequence data before commit.
3. **Allocator ↔ cache invariant.** `alloc.c` invalidates every
   granted block from the cache (`carafs_cache_invalidate`): a stale
   cached image of a previously freed block must never satisfy a
   later `CARAFS_GET_ZERO` hit. Don't remove this.
4. **Verify-on-miss only.** Typed getters (cnode/AG/btree) check
   magic + self-address always but crc only when `carafs_cache_get`
   reports the data was freshly read from the bdev — a dirty cached
   block legitimately has a stale crc until its `*_dirty` finalize
   (`carafs_put_crc`) runs.
5. **Inline extents have no file offsets.** The cnode's 16 inline
   extents are concatenated runs; `start == 0` encodes a hole (block
   0 is the superblock, never a valid start); the last inline extent
   is never a hole. The tree (`tree_root != 0`) keys by file offset
   and encodes holes as absent keys; spilling drops hole runs. A file
   is inline-data XOR extent-mapped, never both.
6. **Generation tombstones.** Delete rewrites the freed cnode block
   with generation+1 and link_count 0; create reads its granted block
   and resumes a legible tombstone's sequence. Best-effort by design
   — reuse as a data block destroys the tombstone.
7. **Single-threaded by contract.** One mounter, no locks; at most a
   handful of pins live at once (the cache floor is
   `CARAFS_CACHE_MIN_BLOCKS` = 16). The Phase 3 home is an AmigaDOS
   handler Gleas which serialises by construction.

---

## 3. What's next: F6 — Subgoal-3 boot path (closes Phase 2)

### F5 shipped (`0da9d95`) — kernel mount over NVMe

CaraFS runs on device storage now; the kernel-side internals:

- **`src/croi/carafs_bind.c` is the binding.** A kernel `CarafsBdev`
  (`g_carafs_bdev`) over `Croi_Nvme_Read/Write/Flush`; one global mount
  `g_carafs` (extern in `cara/carafs_bind.h`), gated by
  `g_carafs_mounted`. `Croi_Carafs_BringUp()` (called from `entry.c`
  right after the I/O queue pair is up) mounts NSID 1, formatting first
  on `EBADMAGIC`/`EBADVERSION`. The FS block size is the 4 KiB default;
  one FS block = `block_size / LBA-size` contiguous LBAs.
- **DMA bounce.** `Croi_Nvme_Read/Write` need a page-aligned direct-map
  buffer ≤ 8 KiB (PRP1+PRP2); cache frames are only 64-byte aligned, so
  every transfer bounces through a `Page_Alloc`'d 2-page buffer in
  ≤8 KiB chunks. The cache arena is 256 KiB of plain BSS (no DMA).
- **`Croi_Nvme_Flush`** (io.c) issues NVM Flush on the I/O queue;
  `bdev->flush` maps to it so the WAL's ordering/commit barriers hold on
  real media. `Carafs_Sync` (checkpoint) before the kernel WFIs is what
  makes a write durable for the next boot.
- **Tests.** `test_carafs.c`: `carafs_mount`, `carafs_io`
  (create/write/read-back/delete on device), `carafs_persist`
  (seed-or-verify a marker). The boot smoke (`smoke_qemu_kernel.sh`)
  now boots QEMU **twice** on one NVMe image and asserts the marker
  seeds on boot 1, verifies on boot 2.
- **Gotchas for next time.** The kernel `LOG` formatter has no `%.*s`
  (and is `%llu`-for-u64); keep new log lines to `%u`/`%llu`/`%x`. The
  existing `nvme_io` test scribbles the namespace tail every boot, which
  clobbers CaraFS's *backup* superblock + a couple of high data blocks —
  harmless because mount uses the primary sb and the persist marker
  lives in low (AG 0) blocks, but don't put anything load-bearing there.

### F6 — Subgoal-3 boot path (complete; design in docs/LOGAIC_BOOT.md)

All four epics shipped. The boot-path doc (`docs/LOGAIC_BOOT.md`)
records the load-bearing decision — Clar reaches the FS via thin kernel
`Croi_Fs_*` syscalls in Phase 2; `dos.library` is Phase 3 (PRINCIPLES
§6). What landed:

- **G1 — GPT discovery (shipped, `b7c80aa`).** `cara_gpt`
  (`src/logaic/gpt`) is a cleanroom GPT parse/format behind a `GptDev`
  seam (IEEE CRC-32, protective MBR + primary/backup headers + 128-entry
  array, the minted `CARAFS_GPT_TYPE_GUID`). `Croi_Carafs_BringUp`
  (carafs_bind.c) now reads the GPT — laying one down on a blank
  namespace — and offsets the mount to the CaraFS partition (LBA 2048).
  `nvme_chunked` is the shared raw-LBA bounce helper for both the GPT and
  CaraFS paths. Host-tested (test_gpt.c); the two-boot smoke discovers
  the GPT on boot 2.
- **G3 — FS syscalls + Clar drawer (shipped, `fe701fb`; criterion met).**
  `SYS_Fs_Read=21` / `SYS_Fs_Write=22` dispatch to `Croi_Fs_Read/Write_Impl`
  (carafs_bind.c) over the boot mount's root directory (read-named-file;
  write with drop+recreate replace semantics — CaraFS has no truncate).
  Clar reads its drawer's `note` file on open and logs `drawer note='…'`
  (the persistence proof, kept in a private buffer so `clar_smoke` is
  unaffected) and writes the typed buffer on Return. The two-boot smoke
  proves it: boot 1 `saved note`, boot 2 `drawer note='as'`.
  `KERNEL_TEST(carafs_fs_syscall)` covers the backends directly.
- **G4 — Startup-Sequence (shipped, `3c3c279`).** `Croi_Boot_RunStartup`
  (carafs_bind.c) resolves `S/Startup-Sequence` (dir `S` → file), parses
  `;` comments / `Echo <text>` / `LoadWB` (unknown commands warn — no
  shell until Phase 3), and returns whether to launch the Workbench.
  entry.c runs it after mount and gates the live Clar launch on it; a
  fresh volume is seeded with a default at format. `KERNEL_TEST(carafs_startup)`
  + the smoke asserts `startup: CaraOS-ready` on every boot.
- **G2 — UUID-aware root selection (shipped, `8c25ad7`).** cara_gpt
  `Gpt_FormatN` (N partitions) + `Gpt_FindCarafsNth` (enumerate) +
  `Gpt_FindByVolumeUuid` (pick the partition whose superblock carries a
  target UUID; generic in magic+uuid-offset so GPT stays unaware of the
  CaraFS sb shape). Host-tested with two partitions. The kernel logs the
  root volume UUID; single-volume boot still mounts the one partition,
  selector ready for multi-volume boot config later. (A host `mkfs --gpt`
  authoring tool was left out — not needed; mkfs/fsck already exist.)

How Clar reaches the FS: direct `ecall`s (see `ecall4` in clar.c) — no
library trampoline needed. Watch-outs from the F5 recap still apply (no
`%.*s` in the kernel `LOG`; `nvme_io` scribbles the namespace tail, now
the backup-GPT region, clear of the partition).

### Phase 3 — AmigaOS Release 2 (V36+) parity (underway; planned in docs/PHASE3.md)

The project's real charter: the AmigaOS V36+ API under verbatim names so
a Release-2 source program builds + runs unmodified. The plan is written
(`docs/PHASE3.md`) with three locked decisions: **breadth-first
library-by-library** (dependency order), **`dos.library` as an AmigaDOS
handler Gleas** (server flavour, retiring the G3 `Croi_Fs_*` stopgap),
and **apps-driven impl with ABI-complete declaration** (each library's
full `.conf`/headers link, bodies implemented to what the editor/paint/
file-manager exercise, the rest defined stubs). Epic order: **P0 ABI
proof ✓ (`9c8429b`)** → **L1 exec (widen, deep) — NEXT** → L2 utility →
L3 dos (handler Gleas) → L4 graphics (Dath CPU raster) → L5 intuition
(widen) → L6 devices → L7 BOOPSI → L8 gadtools → L9 asl → L10–14
iffparse/icon/diskfont/commodities/expansion → T tools → A apps +
PORTING.md (criterion).

**P0 shipped:** `src/userland/v36hello.c` is verbatim-V36 source
(`<exec/*>`/`<intuition/*>`/`<proto/*>` only) — it builds against our
headers and `KERNEL_TEST(v36hello_smoke)` runs it to `RETURN_OK`.
`docs/PORTING.md` is the rebuild-and-link recipe. The pattern for adding
a userland Gleas (CMake target + `user_blob.S` incbin + a spawn
KERNEL_TEST) is now well-trodden (userhello/exec/intuition/clar/v36hello).

**L1 — widen `exec.library` (in progress).**

- **Slice 1 shipped (`92b4557`): the list primitives** (Insert/AddHead/
  AddTail/Remove/RemHead/RemTail/Enqueue/FindName) — the **first
  U-mode-callable `local`-flavour LVOs**. The pattern, now established
  for the whole phase: declare in the `.conf` as `local` at canonical
  LVOs (split `##pad_run`s to keep ordinal == LVO position); implement
  in a `.lib_text.exec` source file (the 0x4000_0000 RX page) that is
  **self-contained — no kernel symbols, no globals, no out-of-section
  calls** (inline any helper as a macro; a real `static inline` that
  doesn't inline becomes a PC-relative call that overflows). **Test
  from a Gleas, never a KERNEL_TEST** — S-mode can't fetch the
  User-mapped RX page (instruction fault). `userexec.c` exercises them
  via the proto stubs.
- **Slice 2 shipped (`5d0dce6`): `AllocVec`/`FreeVec`** (syscall; LVOs
  -684/-690, ords 113/114 after a `##pad_run 21`). The full syscall
  recipe in one go: `cara/sysno.h` number + `exec.conf` line +
  `trampolines.S` `CARA_SYSCALL_TRAMPOLINE` + `syscall.c` arm +
  `Croi_*_Impl` (mem.c) + `cara/exec_lib.h` decl, exercised in
  `userexec.c`.
- **Slice 3 shipped (`1763f81`): messaging** — `CreateMsgPort` (no-arg
  LVO; allocs a signal on `Sched_Current()` + a public `struct MsgPort`
  via the internal `Croi_CreateMsgPort`, returns `&p->pub`),
  `DeleteMsgPort`, `ReplyMsg`. The V36 message cycle is now complete;
  `userexec.c` does a full PutMsg→GetMsg→ReplyMsg→GetMsg round-trip.
  Confirmed lvo-gen accepts a no-arg `()` LVO.
- **Slice 4 shipped (`7c173b3`): `FindTask` + `CopyMem`/`CopyMemQuick`.**
  FindTask (syscall): NULL→`Sched_Current()` (the kernel Task *is* the
  public `struct Task`); named lookup self-only (no global named-task
  registry). **Gotcha: the returned `Task` is kernel-resident → opaque
  to U-mode (don't dereference it).** Tasks aren't in the SASOS shared
  heap, unlike MsgPorts — a real Phase-3 gap (move Task/Process to shared
  memory, or hand out a shared shadow, before programs read `tc_*`).
  CopyMem/CopyMemQuick are `local` (copy_ops.c, RX page).
- **Slice 5 shipped (`af24dde`): signal semaphores** (Init/Obtain/
  Release/Attempt) over the new `<exec/semaphores.h>`. Contended Obtain
  blocks on `SIGF_SINGLE`; Release hands off to the queued waiter. To get
  a reserved wait bit, **`AllocSignal` now reserves signals 0..15** (V36+
  convention; returns 16..31) and `SIGB_SINGLE`=bit 4 is in
  `exec/tasks.h`. Gotcha-to-know: **the contended cross-task path has no
  test yet** (needs two tasks; uncontended/nesting is covered in
  userexec). The semaphore struct is caller-owned (SASOS), so unlike
  Task the app *can* read `ss_Owner`/`ss_NestCount` back.
- **Slice 6 shipped (`2dc5e58`): Forbid/Permit/Disable/Enable**
  (task-switch control, syscall). Cooperative single-hart → they gate
  `Croi_Yield` (no preemption to mask) via the running task's existing
  V36 ABI nest counts `tc_TDNestCnt`/`tc_IDNestCnt` (start -1; switch
  disabled while either >= 0; `Wait` still breaks Forbid). Proven by
  `KERNEL_TEST(exec_forbid)` — a **real two-task test** (Forbid blocks a
  higher-pri worker across Yield; Permit releases). That's the test
  shape the **contended-semaphore path** (slice 5) still needs.
- **L1 CLOSED (`92e256d`): stub-coverage report.** `lvo-gen --coverage`
  → `docs/LVO_COVERAGE.md` (regenerate: `cmake --build build-host
  --target lvo-coverage`). Settles PHASE3.md §7 Q4 — per-LVO stub policy
  (`##pad_run` → `Croi_LvoUnimplemented`, ordinal frozen). Current
  coverage: exec **33%** (37 impl / 74 stub), intuition **6%** (5/68),
  cruth **100%** (9 impl + 10 server). This is the L1 done-criterion (d).
  The exec `.conf` is intentionally *not* yet the full Exec autodoc — the
  74 stubs are the long-tail, declared as stubs so the gap is visible;
  widening to ABI-complete is incremental and low-priority.
- **L2 slice 1 shipped (`6a4b462`): `utility.library`** tag-list
  walkers + Hook dispatch. utility is base-less helper code → **every
  implemented LVO is `local` flavour**, self-contained in a new
  `.lib_text.utility` RX page (`src/croi/utility_lib/tag_ops.c`), JALR'd
  straight from U-mode (no syscall) — exactly exec's `list_ops.c`
  pattern. Done: `FindTagItem`, `GetTagData`, `NextTagItem` (the
  iterator; resolves TAG_IGNORE/TAG_MORE/TAG_SKIP/TAG_DONE),
  `PackBoolTags`, `TagInArray`, `CallHookPkt`. New headers
  `<utility/utilitybase.h>` + `<utility/hooks.h>` (verbatim V36). Library
  built like intuition (base + vec in SASOS shared heap, entry.c
  MakeLibrary), but vec targets are local impls not trampolines. Coverage
  **23%** (6 impl / 20 stub). userexec opens it and exercises all six.
  **New-library recipe, captured:** new `<lib>.conf` (`##base_type`
  needs a `<lib>/<lib>base.h>`); `src/croi/<owner>/` with the local
  impls (`LIBTEXT_<lib>` section macro) + reserved hooks (ordinary kernel
  text); `add_subdirectory` in `src/CMakeLists.txt`; whole-archive the
  static lib into `croi`; `KEEP(*(.lib_text.<lib>))` in `croi.lds`;
  `extern <lib>_lib_vec[]` + MakeLibrary block in `entry.c`; add the conf
  to the `lvo-coverage` target; `add_dependencies(<app> cara_<lib>_gen)`.
- **L2 slice 2 shipped (`5870c8b`): `MapTags`/`FilterTagItems`/
  `FilterTagChanges`** — the pure tag-transform trio, all `local`
  (in-place, `.lib_text.utility` RX page). `MAP_*`/`TAGFILTER_*` consts
  now in `<utility/tagitem.h>`. Documented flat-list operation (avoids
  in-place compaction across TAG_MORE chains; the common usage).
  utility coverage **34%** (9 impl / 17 stub). userexec exercises all.
- **L2 slice 3 shipped (`e52d812`): allocating tag helpers**
  `AllocateTagItems`/`CloneTagItems`/`FreeTagItems`/`RefreshTagItemClones`
  — utility's first `syscall`-flavour rows (they allocate, so not
  `local`). New `src/croi/utility_lib/trampolines.S` (.lib_text.utility)
  + `tag_alloc.c` kernel impls over the SASOS heap (`Croi_AllocVec_Impl`
  size-header → FreeTagItems frees by pointer; MEMF_CLEAR → fresh list
  reads all-TAG_DONE). sysno 37..40; `cara/utility_lib.h` decls.
- **L2 effectively COMPLETE: utility coverage 50%** (13 impl / 13 stub).
  The full **tag-list + Hook surface** is implemented; the remaining 13
  stubs are the 2 reserved private slots + the V37 date/math/string
  functions (`Amiga2Date`/`SMult32`/`Stricmp`/…) — pure leaf functions,
  implement on demand if an app needs them (all could be `local`).
- **L3 — `dos.library` (Logaic): COMPLETE (L3.1..L3.7, `docs/LOGAIC_DOS.md`).**
  The keystone epic, shipped. 19 impl LVOs (locks, file I/O, dir mutation,
  Info, process I/O + console, Delay, IoErr); the `Croi_Fs_*` stopgap is
  retired and Clar runs on dos. Decisions as locked in the doc — **note**
  one is now stale vs the code: **BPTR** shipped as a real pointer
  (`void *`, no `>>2`), not the `addr>>2` the doc first proposed (see the
  L3.1 note below). The doc otherwise reflects what shipped. Original
  scoping was `84b37f9`. Decisions locked there: **BPTR** = (proposed)
  `addr>>2` widened to `IPTR`
  (BADDR/MKBADDR; convert only at the dos edge); **Process** = U-mode
  Gleas `struct Task` embedded at the front of a shared-heap
  `struct Process` (makes `(struct Process *)FindTask(NULL)` legal in
  U-mode — *also fixes the L1 FindTask-opacity gap*); **architecture** =
  U-mode Logaic handler Gleas, `server`-flavour `DosPacket`/`ACTION_*`
  ops, reaching CaraFS via a handler-only cnode/lock-level kernel FS
  syscall surface (the cnode API in `carafs.h`: `CnodeStat`/`FileRead`/
  `FileWrite`/`DirLookup`/`DirNext`/…). L3 builds the **deferred
  server-flavour call path** (LVO.md §12: real lvo-gen server stub +
  libcara per-task reply port + synchronous PutMsg/WaitPort) — reusable
  by L4/L6. Seven slices L3.1..L3.7; **L3.7 retires the `Croi_Fs_*`
  stopgap** (delete the app-facing name-in-root `SYS_Fs_Read/Write`,
  repoint Clar to dos Open/Read/Write/Close; the handler-only FS syscalls
  stay).
- **L3.1 shipped (`014ca2b`): dos ABI + Process model.** New headers
  `<dos/dos.h>` + `<dos/dosextens.h>`; `dos.conf` (to IoErr -132, rest
  stubs); `dos.library` constructed at boot (Logaic, `src/logaic/dos/`);
  `IoErr()` (syscall). **The Task→shared-heap-Process move landed**
  (`Croi_SpawnUserTask`/`...FromElf` now `Croi_AllocShared(sizeof(struct
  Process))`, Task at `pr_Task` offset 0; `Croi_Free` auto-routes) — so
  `(struct Process*)FindTask(NULL)` is legal in U-mode and **the L1
  FindTask-opacity gap is fixed**. BPTR decided as real pointer (`void*`,
  no `>>2`, per existing `exec/types.h`); BSTR widened to BPTR. dos
  coverage 5%. **GOTCHAS for later slices:** (a) dos file `Open` (-30)
  collides with lvo-gen's reserved-slot name `Open` — must disambiguate
  reserved-slot CARA_IDX/naming in lvo-gen before **L3.4**; (b) `IoErr`
  casts `Sched_Current()` to `Process*` — only valid because syscalls
  only come from U-mode Gleasanna (all Processes); kernel tasks aren't.
- **L3.2 shipped (`ba08b31`): dos handler + U-mode packet round-trip.**
  The dos handler is a **kernel-resident server task** (`dos.handler`,
  pri 120) — *v0 deviation from the scoping doc*: it owns the CaraFS
  mount (S-mode) so real actions call `Carafs_*` directly, no handler-
  only FS syscalls. `Croi_Dos_StartHandler()` (entry.c, after dos.library
  construction) spawns it + yields so its port publishes before tests
  run; the loop is WaitPort/GetMsg → dispatch `dp_Type` → ReplyMsg
  (L3.2: ACTION_NIL echo). `SYS_Dos_HandlerPort` (42) hands the port to
  U-mode. libcara now provides `memset`/`memcpy` (freestanding-required).
  userexec drives a full DosPacket round-trip (StandardPacket on its
  stack → PutMsg → WaitPort own reply port → echoed result). The U-mode
  server-stub call path is now proven end to end.
- **L3.3 locks shipped (`3af2c52`).** `Lock`/`UnLock`/`DupLock`/
  `CurrentDir` (`syscall` flavour). **Decision (a) RESOLVED:** dos packet
  ops are `syscall` flavour calling the shared **`Croi_Dos_Dispatch`**
  directly (no server-stub codegen, no per-task reply port, no round-trip)
  — CaraFS is synchronous + cooperative single-hart makes the syscall
  atomic; the handler task shares the same dispatch for the U-mode
  PutMsg path. `DosPacket` `dp_Res*`/`dp_Arg*` widened `LONG`→`SIPTR`
  (carry 64-bit pointers). FileLock.fl_Key = cnode; path resolve via
  `Carafs_DirLookup` (`/`-split, `:`→root, relative to `pr_CurrentDir`).
  dos coverage 27% (5/13).
- **L3.3b shipped (`3725f1a`): Examine/ExNext.** `ACTION_EXAMINE_OBJECT`/
  `_NEXT` in dispatch over `Carafs_CnodeStat`/`Carafs_DirNext`. A
  `FileLock` is now the head of a kernel-private **`struct DosLockExt`**
  (in handler.c) carrying the `CarafsDirCursor` (ExNext resume) + the
  object's own name (a cnode can't be reverse-mapped to a name, so Lock
  stashes the resolved final component). `fill_fib` maps `CarafsStat`→FIB;
  **`fib_Date` conversion (CaraFS ns → AmigaDOS DateStamp) is deferred /
  zeroed** — a known v0 gap. dos coverage **38%** (7 impl / 11 stub).
  **L3.3 complete** (locks + examine).
- **lvo-gen blocker cleared (`1b64e8e`):** reserved slots now emit
  `CARA_IDX_LIB_OPEN`/`_CLOSE`/`_EXPUNGE`/`_EXTFUNC` + the dup-name
  validator skips ord<4, freeing verbatim names for real LVOs. Golden
  files regenerated. (If you add a library row named Open/Close/Expunge/
  ExtFunc, this is why it works now.)
- **L3.4 shipped (`7c0584e`): file I/O.** `Open`/`Close`/`Read`/`Write`/
  `Seek` (syscall). Dispatch: `ACTION_FINDINPUT`/`FINDOUTPUT`/`FINDUPDATE`
  (Open by mode; NEWFILE truncates via DirRemove+DirCreate since CaraFS
  writes are extend-only), `ACTION_READ`/`WRITE` over `Carafs_FileRead`/
  `FileWrite` at the handle's pos, `ACTION_SEEK`, `ACTION_END`. An open
  FileHandle is the head of a kernel-private `struct DosFileExt` (u64
  cnode + pos). `dos_resolve_parent` splits path → parent dir + final
  component for create. dos coverage **66%** (12 impl / 6 stub).
- **L3.5 shipped (`cf310c9`): mutation + info.** `CreateDir`/`DeleteFile`/
  `Rename`/`Info` (syscall). Dispatch: `ACTION_DELETE_OBJECT`
  (DirRemove), `ACTION_RENAME_OBJECT` (DirLink new + DirRemove old —
  **files only**, CaraFS has no dir hard links), `ACTION_CREATE_DIR`
  (DirCreate → lock), `ACTION_DISK_INFO` (struct InfoData from the
  superblock). `SetProtection`/`SetComment`/`SetFileDate` NOT done —
  **CaraFS has no attribute setters**; revisit when one lands. dos
  coverage **88%** (16 impl / 2 stub; only Input/Output left in the
  declared surface).
- **L3.6 shipped (`7db275d`): process I/O + console + Delay.**
  `Output`(-60)/`Input`(-54) return the Process's `pr_COS`/`pr_CIS`,
  lazily populated with a v0 console FileHandle. The console is inline in
  `Croi_Dos_Dispatch` via a kernel-private handle **kind**
  (`CARA_FH_CONSOLE`, vs `CARA_FH_FILE` for CaraFS handles):
  `ACTION_WRITE` → kernel log (tag `cout`, chunked to the log record
  cap), `ACTION_READ` → immediate EOF (stub stdin), `ACTION_SEEK` →
  error. `Delay`(-198) is a `syscall` busy-yield shim over `Croi_Time`
  (spin-yield until `ticks/50 s`; 1 tick = 20 ms). **Also fixed** a
  latent non-idempotency: CaraFS persists across the boot smoke's two
  boots, so the L3.5 mutation test hit EXISTS on boot2 — the test now
  clears `uexec-dir`/`ren-*.txt` first. dos coverage **65%** (19 impl /
  10 stub; ABI now declared through `Delay` at ordinal 32, widening the
  pad gap, hence the percentage drop).
- **L3.7 shipped (`d10983b`): retired the `Croi_Fs_*` stopgap — L3
  COMPLETE.** Clar's drawer note now uses dos `Open`/`Read`/`Write`/
  `Close` (opens `dos.library` v36 + a `DOSBase` global, like any V36
  program) instead of `SYS_Fs_Read`/`Write`. Deleted: the syscall arms
  (`sysno` 21/22 left **reserved** so the trampoline wire ABI is never
  reused), `Croi_Fs_Read/Write_Impl` + `copy_name` in `carafs_bind.c`,
  and the `carafs_fs_syscall` kernel test (31/0 now). No "handler-only FS
  syscalls" survive — the v0 handler is kernel-resident and calls
  `Carafs_*` directly (the L3.2 deviation), so nothing is left behind.
  The Phase-2 criterion now holds **via dos**: the boot smoke's two-boot
  `drawer note='as'` check drives Clar persistence through dos.library.
- **L4.1 shipped (`8eff42f`): graphics.library ABI + GfxBase + boot
  construction.** `graphics.conf` (`##base GfxBase`, `##owner croi/dath`;
  reserved hooks + a 150-entry `##pad_run` out to FreeBitMap -924);
  `include/graphics/gfxbase.h`; `src/croi/dath/graphics_hooks.c`; a
  riscv64-only `cara_graphics_lib` (hooks + generated vec) whole-archived
  into croi, keeping `cara_dath` the pure dual-target rasteriser;
  `Croi_MakeLibrary` block in entry.c. userexec opens it v36 + checks
  version. Coverage **0%** (0 impl / 150 stub / 4 reserved) — expected
  baseline; drawing LVOs land next.
- **L4.2 shipped (`ce1c41b`): BitMap alloc + RastPort init + SetRast.**
  `struct BitMap`/`RastPort` filled to V36 ABI; the `DathBitMapExt`
  bridge (`{BitMap bm; DathFramebuffer surf;}`, BitMap at offset 0).
  `AllocBitMap` allocates a chunky surface in the **shared heap**
  (`Croi_AllocShared` — U-mode-readable pixels; depth<=16→RGB565 else
  RGBA8888), `Planes[0]` = chunky buffer; `FreeBitMap`; `InitRastPort`
  (V36 defaults); `SetRast`→`Dath_Clear`. **Pen model refinement:** pens
  are *indices* into a v0 default 8-entry palette (the minimal ColorMap),
  not raw colours — `RastPort.FgPen` is a `BYTE`, so the scope's loose
  "direct-colour" wording (§3/§6.2) became a small fixed palette;
  `SetRGB4`/`LoadRGB4` will override it later. First `syscall` gfx rows
  (`.lib_text.graphics` trampolines, `SYS_Gfx_*` 61-64); coverage **2%**
  (4 impl / 146 stub).
- **L4.3 shipped (`15db462`): pen state + primitives.**
  `SetAPen`/`SetBPen`/`SetDrMd` (RastPort state), `Move`/`Draw` (cursor +
  `Dath_DrawLine`), `WritePixel`/`ReadPixel` (`Dath_Pixel` / raw surface
  read; 0/-1 / value/-1), `RectFill` (`Dath_FillRect`, inclusive
  corners). FgPen resolved through the default palette; JAM1/JAM2 draw
  identically for primitives, COMPLEMENT unimplemented (v0). `SYS_Gfx_*`
  65-72. Coverage **8%** (12 impl / 138 stub).
- **L4.4 shipped (`7d51b32`): blits.** `BltBitMap`/`BltBitMapRastPort`/
  `ClipBlit` over `Dath_BlitRect`. **ABI lesson:** these exceed the
  **7-register syscall limit** (BltBitMap 11 args, others 9; the
  trampoline burns `a7` on the syscall number), so they're **`local`
  marshalling stubs** in `.lib_text.graphics` (`graphics_blit.c`) that
  pack a `GfxBltArgs` and make one `SYS_Gfx_Blt` ecall — one kernel impl
  for all three. The ecall is **inlined per-stub** (a non-inlined static
  helper = out-of-section call → PC-rel overflow, the L1 RX-page gotcha).
  This `local`-stub pattern is the template for any wide LVO (L5 will
  need it). v0: plain copy, **same-format only** (`Dath_BlitRect` no-ops
  on mismatch), `ClipBlit` clips to the BitMap rect (no layers).
  `ScrollRaster` deferred (overlap-safety). Coverage **10%** (15 impl /
  135 stub).
- **L4.5 shipped (`c9f6492`): text + fonts.** `OpenFont`/`CloseFont`/
  `SetFont`/`Text`/`TextLength` over the Dath 8x8 font. `struct TextFont`
  filled (ABI); v0 has **one face** — OpenFont returns a single
  shared-heap `TextFont` over `dath_font_8x8` regardless of the
  `TextAttr`, and the rasteriser renders from `dath_font_8x8` directly
  (the strike-format char data is unpopulated). `Text` → `Dath_DrawChar`
  loop, FgPen/BgPen, cursor advance; **v0 treats cp_y as the glyph TOP**
  (not the AmigaDOS baseline, so cp_y=0 is visible / matches Leargas).
  All ≤3 args → normal syscall trampolines (`SYS_Gfx_*` 74-78). Coverage
  **13%** (20 impl / 130 stub). **The core graphics surface is now in**
  (alloc, RastPort, pen, primitives, blits, text) — what intuition + a
  basic paint app need.
- **L4.6 shipped (`ea39d2a`): area fill.** `InitArea`/`AreaMove`/
  `AreaDraw`/`AreaEnd` over a new **even-odd scanline polygon fill**
  (`fill_polygon` in graphics_lib.c; half-open `[ylo,yhi)` edge rule,
  spans via `Dath_FillRect`, 64 pts/poly v0 cap). `struct AreaInfo`
  filled (ABI); AreaMove/Draw append vertices into the app's InitArea
  buffer (SUM=1), AreaEnd fills + resets. `SYS_Gfx_*` 79-82. **`Flood`
  (seed fill) deferred** — separate queue algorithm, apps-gated (§6.3).
  Coverage **16%** (24 impl / 126 stub). **L4 core + areas done.**
- **L4.7 shipped (`95a593d`): live Screen.RastPort — L4 COMPLETE.**
  `struct LeargasScreen` embeds a `RastPort` + `DathBitMapExt` whose surf
  mirrors the screen `*fb`; `Screen.RastPort`/`Screen.BitMap` (previously
  `nullptr`) now point at them, so apps + L5 window code can draw through
  graphics.library against the same pixels Leargas chrome renders to.
  **Dual-target constraint:** the setup is pure data (V36 RastPort
  defaults inline) so the host-buildable screen code does NOT call the
  kernel-only `Croi_Gfx_*` impls; the existing chrome rendering keeps its
  `Dath_*` path (same pixels). KERNEL_TEST(`graphics_screen_rastport`)
  draws through `Screen.RastPort` via the gfx impls + verifies the fb.
  No LVO change (coverage still 16%). kernel 32/0.
- **NEXT: scope L5 — `intuition.library` widen.** The big apps unblock:
  screens (`OpenScreen`/`OpenScreenTagList`), the tag window opener
  (`OpenWindowTagList`), menus, requesters, the full gadget + IDCMP
  surface. Its graphics dependency (RastPorts, text, blits) is now
  satisfied. Write a `docs/LEARGAS_INTUITION.md` scope (as L3/L4 got
  design docs) before cutting code. Window RastPorts derive from the
  screen RastPort just landed. (Today intuition is 5 LVOs at ~6%
  coverage — `OpenWindow`/`CloseWindow`/gadget attach/`ActivateGadget`.)
- **L4 reference: `graphics.library` (Dath): SCOPED (`docs/DATH_GRAPHICS.md`).**
  Read that doc before cutting L4 code. Decisions locked there: (1) **chunky
  RTG bitmaps, not planar** — `struct BitMap` kept ABI-shaped but opaque,
  head of a kernel-private `DathBitMapExt` carrying the chunky
  `DathFramebuffer` (the L3 ext pattern); `AllocBitMap` (a V39 LVO) is the
  primary drawable-BitMap ctor; planar `AllocRaster`/`InitBitMap` declared
  for ABI only. (2) **`syscall`-flavour rasterisation** on the *in-place
  kernel Dath rasteriser* (not `local`) — reuses the existing kernel
  rendering path intuition already uses, keeps the framebuffer kernel-VA,
  and Phase-4's GPU supersedes the hot path anyway. (3) **RastPort lives
  with the caller** (stack/shared, read via SUM=1); the BitMap carries the
  surface, so one path serves off-screen + screen targets. Slice plan
  L4.1 ABI+GfxBase+bridge → L4.2 AllocBitMap/InitRastPort/SetRast → L4.3
  pen+line/pixel/rectfill → L4.4 blits → L4.5 text+fonts → L4.6 areas/flood
  (apps-gated) → L4.7 Leargas convergence (optional). Test = a
  userexec-style Gleas drawing into an off-screen shared-heap chunky BitMap
  and reading pixels back. The Dath substrate exists (`src/croi/dath`).
- **Open L3 gaps (tracked, not blocking):** **fib_Date** (CaraFS ns →
  AmigaDOS DateStamp) still deferred/zeroed; **Rename** is file-only (no
  dir rename); handler concurrency still v0-serial; console stdin is an
  EOF stub + `Delay` is a busy-wait (real stdin + a deadline-timer block
  can pull a `timer.device`-shaped dep forward from L6); no
  `SetProtection`/`SetComment`/`SetFileDate` (CaraFS setter gap).
- **Deferred exec long-tail (optional, low-value):** `MakeLibrary`/
  `MakeFunctions`/`SetFunction`/`SumLibrary` (library-author API;
  `Croi_MakeLibrary` is the substrate). Device prims (`OpenDevice`/`DoIO`/
  …) are **L6**, not L1 (need the device model first).

Open questions still open in PHASE3.md §7: BSTR/BPTR site, Process-vs-Task
layout, `proto/` search path, which sample apps. (§7 Q4 stub-coverage:
RESOLVED.)

---

## 4. Build / test / commit workflow

**Two build dirs** (`CARA_TARGET` fixed per dir). The rv64 build needs
the host-built `lvo-gen`; build host first, rv64 auto-detects it.

```bash
# Host: tools + portable modules + unit tests
cmake -S . -B build-host
cmake --build build-host -j8
ctest --test-dir build-host                       # 24/24 expected

# RV64 kernel (auto-detects build-host/tools/lvo-gen/lvo-gen)
cmake -S . -B build-rv64 -DCARA_TARGET=riscv64 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake
cmake --build build-rv64 -j8                      # -Werror; produces src/croi/croi.elf
```

**The "green" gate before any commit** (the standing rule — commit
automatically at the end of a green epic; project memory
`commit-at-green-epic-end`):

```bash
ctest --test-dir build-host                                # 24/24, 0 failed
bash tests/boot/smoke_qemu_kernel.sh "$(command -v qemu-system-riscv64)" \
     build-rv64/src/croi/croi.elf                          # "smoke_qemu_kernel: ok"
cmake --build build-host --target format-check             # PASS
```

Gotchas:

- **Run the kernel WITH its devices** for an accurate test count: the
  xHCI tests need `-device qemu-xhci -device usb-kbd -device
  usb-mouse`, the nvme tests need a `-device nvme,...` with a backing
  image. The smoke harness wires all of it (including a throwaway
  NVMe image); missing devices = those tests fail, which is expected,
  not a regression. Full incantation: see `tests/boot/smoke_qemu_kernel.sh`.
- **clang-format**: format with Homebrew LLVM 22's clang-format
  (`cmake --build build-host --target format`); the tree was
  reformatted to it. Keep cosmetic churn on *pre-existing* files in a
  separate `style:` commit (project memory `clang-format-version-skew`).
- The in-editor LSP shows `'cara/types.h' file not found` etc. —
  standalone LSP missing `-Iinclude`; ignore. The CMake build has the
  include paths.
- Commit style: subject `phase-N/<Epic>: <short>`, bullet body, end
  with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
  Commit to `main` (linear, trunk-based history).
- **Do not commit** `amiga_docs/` (large reference PDFs) or
  `CLAUDE.md` unless asked.

---

## 5. Phase 1 demo (kept for reference)

The Phase 1 success criterion is met live: boot, see the Workbench
desktop, click the "Bosca" drawer → child window with a text Inntin →
type → captured. To see it on macOS:

```bash
qemu-system-riscv64 -M virt -m 256 -display cocoa -serial stdio \
  -kernel build-rv64/src/croi/croi.elf \
  -device qemu-xhci -device usb-kbd -device usb-mouse -device ramfb
```

Click the drawer, type into the field (`Cmd` releases the mouse
grab). The `clar_smoke` KERNEL_TEST drives the same interaction
headlessly. A `qemu-virt-clar` cmake target for this incantation is
still an open polish item, along with window dragging, desktop
re-focus after child close, a real drawer glyph, and key-up/N-key
rollover in the input pump.

---

## 6. Quick orientation map

- `docs/CARAFS.md` — the filesystem design + epic plan (current work).
- `docs/ARCHITECTURE.md` — system design (SASOS §4, Kobj/Handles §5,
  IPC §6, libraries/LVO §7). `docs/LVO.md` — the library-bridge model.
- `include/cara/carafs.h` — CaraFS on-disk format + public API.
- `src/logaic/carafs/` — the CaraFS core (see §2 for the file map);
  `internal.h` is the internal seam catalogue.
- `tools/carafs/` — hosted mkfs.carafs / fsck.carafs.
- `tests/unit/test_carafs_*.c` — format / mkfs-fsck / file suites.
- `include/cara/nvme.h`, `src/croi/nvme/` — the NVMe driver (F5 binds
  CaraFS to it).
- `src/croi/leargas/`, `src/userland/clar.c` — the Phase 1 window
  system + Workbench Gleas (Phase 2's success criterion touches Clar).
- `src/croi/tests/` — in-kernel `KERNEL_TEST()`s; `tests/boot/` — the
  boot smoke gate.
