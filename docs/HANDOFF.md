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
33fb3ab phase-3/L13.1 commodities.library foundation + the CxObj object model (live input dispatch deferred)
bdb3c99 docs          scope L13 commodities.library (docs/COMMODITIES.md)
88b2e25 phase-3/L12.3 diskfont AvailFonts + FontContents helpers — closes L12 (asl wiring deferred)
895da18 phase-3/L12.2 diskfont.library + OpenDiskFont + the Cara font codec (FONTS: dos path → TextFont)
ffb6b0a phase-3/L12.1 Dath text renders from a generic TextFont strike (topaz reframed; foundation for disk fonts)
3d03087 docs          scope L12 diskfont.library (docs/DISKFONT.md)
80f476c phase-3/L11.3 icon.library tool-type/revision helpers — closes L11 (FindToolType/MatchToolValue/BumpRevision)
314d0d9 phase-3/L11.2 icon.library write side + defaults (PutDiskObject/DeleteDiskObject/GetDiskObjectNew/Get+PutDefDiskObject)
8dabe86 phase-3/L11.1 icon.library foundation + CaraFS xattr layer + GetDiskObject
38a97b4 docs          scope L11 icon.library (docs/ICON.md)
8dd9182 phase-3/L10.4 iffparse props/collections — closes L10 (PropChunk/FindProp/CollectionChunk/FindCollection)
d4d275a phase-3/L10.3 iffparse write side (PushChunk/PopChunk/WriteChunkBytes)
16d76cc phase-3/L10.2 iffparse read walk (ParseIFF/StopChunk/ReadChunkBytes/CurrentChunk)
fbb3997 phase-3/L10.1 iffparse.library + handle lifecycle (AllocIFF/Open/Close/Free/InitIFFasDOS)
18a98d2 docs          scope L10 iffparse.library (docs/IFFPARSE.md)
c445139 phase-3/L9.3  asl font + screen-mode requesters — closes L9
b96d14f phase-3/L9.2  asl file requester (AslRequest) + legacy AllocFileRequest/RequestFile
a8de180 phase-3/L9.1  asl.library + AllocAslRequest/FreeAslRequest
a8b33c7 docs          scope L9 asl.library (docs/LEARGAS_ASL.md)
c8511f2 phase-3/L8.5  prop gadgets (SLIDER/SCROLLER) + router drag-tracking
7298c24 phase-3/L8.4  gadtools GT_GetIMsg + menu builder
3282201 phase-3/L8.3  gadtools choice/edit kinds + DrawBevelBoxA + GT_GetGadgetAttrsA
fa70150 phase-3/L8.2  gadtools CreateGadgetA + easy kinds + GT_SetGadgetAttrsA
5b83be4 phase-3/L8.1  gadtools.library + render context (VisualInfo/CreateContext)
5bbf845 docs          scope L8 gadtools (docs/LEARGAS_GADTOOLS.md)
f4bec1f phase-3/L7.3  BOOPSI public class registry (AddClass/RemoveClass) — closes L7
4e674a5 phase-3/L7.2  BOOPSI attributes + object lists (SetAttrs/GetAttr/NextObject)
188d118 phase-3/L7.1  BOOPSI class/object core + rootclass (MakeClass/NewObject/DoMethod)
66f3944 docs          scope L7 BOOPSI (docs/LEARGAS_BOOPSI.md)
1448f13 phase-3/L6.4  input.device (IND_WRITEEVENT → Leargas ring) — closes L6
f74821f phase-3/L6.3  console.device (CMD_WRITE → L3.6 cout sink, CMD_READ EOF)
76700e7 phase-3/L6.2  timer.device (TR_GETSYSTIME/TR_ADDREQUEST over Croi_Time)
b599c77 phase-3/L6.1  exec device IO primitives + device registry (OpenDevice/DoIO/…)
378555f phase-3/L5.6  intuition screens (OpenScreen/OpenScreenTagList/CloseScreen) — L5 complete
c7ff55c phase-3/L5    intuition requesters (AutoRequest/EasyRequestArgs)
7fdf02b phase-3/L5.5  intuition rendering helpers + gadget widen (DrawBorder/PrintIText/AddGList/…)
d5a1665 phase-3/L5.4  intuition feedback/timing (CurrentTime/DoubleClick/DisplayBeep/…)
f0d6bd5 phase-3/L5.3  intuition menus (SetMenuStrip/ItemAddress/IDCMP_MENUPICK)
5c0426f phase-3/L5.2  intuition window ops + activation (Move/Size/ToFront/Activate/…)
872117c phase-3/L5.1  intuition tag window opener + window RPort + ModifyIDCMP
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
**52 passed / 0 failed** (L10.1 `iffparse_lifecycle`; L10.2/L10.3
extended `userexec_smoke` with the IFF write→read round-trip), QEMU
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
- **L5 — `intuition.library` widen: SCOPED (`docs/LEARGAS_INTUITION.md`).**
  Read that doc before cutting L5 code. Decisions locked there: (1) **tag
  openers map onto the existing Leargas substrate** — `OpenWindowTagList`
  builds a `NewWindow` from `WA_*` tags (L2 utility walkers) → the
  existing `Leargas_OpenWindow`; the `*TagList` form is the real LVO,
  `*Tags` are header inlines. (2) **window RPort = a "sub-bitmap" view
  onto the screen framebuffer** — a `DathBitMapExt` whose surf is the
  screen with base offset to the window content origin + inner dims +
  screen stride, so window-relative drawing lands correctly *and*
  auto-clips to the window (reuses L4); immediate, no backing store, **no
  occlusion** (non-overlapping windows only; layers later). (3) **menus
  are a new Leargas substrate** (bar render + menu-button router +
  `IDCMP_MENUPICK` + `ItemAddress`). (4) **requesters = modal windows**
  (`AutoRequest`/`EasyRequestArgs` over the bool-gadget+IDCMP substrate).
  All `syscall` flavour (Leargas kernel-resident); wide LVOs use the L4.4
  `local` marshalling-stub pattern. Slice plan L5.1 tag opener+window
  RPort → L5.2 window ops+activation → L5.3 menus → L5.4 requesters → L5.5
  rendering helpers+gadget widen → L5.6 screens (apps-gated).
- **L5.1 shipped (`872117c`): tag window opener + window RPort.**
  `OpenWindowTagList` (`WA_*` → `NewWindow` → `Leargas_OpenWindow`;
  taglist walked kernel-side since utility's walkers are local U-mode
  code); `window->RPort` = a live RastPort over a **sub-bitmap view onto
  the screen** (screen stride, base offset to the window content origin,
  sized to the inner area → window-relative + auto-clipped, immediate, no
  occlusion); `ModifyIDCMP`. New `WA_*` tags; `##include <utility/
  tagitem.h>` in the conf so the generated proto sees `struct TagItem`.
  KERNEL_TEST verifies a tagged window's RPort draws to the right screen
  region + clips at the edge. intuition coverage **7%** (7 impl / 90
  stub; surface declared to -606).
- **L5.2 shipped (`5c0426f`): window ops + activation.** `MoveWindow`/
  `SizeWindow` (clear old region, update geometry, recompute the RPort
  sub-bitmap via the extracted `setup_rport()`, re-render; Size clamps to
  Min/Max), `WindowToFront`/`WindowToBack` (reorder the screen window
  list), `SetWindowTitles` ((STRPTR)-1 = unchanged), `ActivateWindow`
  (focus + `IDCMP_ACTIVE`/`INACTIVEWINDOW` via new generic
  `Leargas_IDCMP_PostClass`). **Fixed a latent focus bug:**
  `Leargas_CloseWindow` now clears the active-window pointer (else it
  dangles and a later `ActivateWindow` short-circuits on heap reuse).
  `SYS_*` 85-90. Coverage **13%** (13 impl / 84 stub).
- **L5.3 shipped (`f0d6bd5`): menus.** `struct Menu`/`MenuItem` + the
  FULLMENUNUM packing macros; new Leargas menu engine (`menu.c`):
  `SetMenuStrip` (lays out bar + dropdown boxes into the structs),
  `ClearMenuStrip`, `ItemAddress`; `Menu_Begin`/`Menu_End` drive the RMB
  pick (render → hit-test → `IDCMP_MENUPICK` with the packed code, then
  full-refresh). RBUTTON wired in router.c; `Leargas_IDCMP_PostMenuPick`
  installed at boot. v0: flat text items, all dropdowns shown at once, no
  sub-menus/imagery/command-keys. Coverage **16%** (16 impl / 81 stub).
- **L5.4 shipped (`d5a1665`): feedback/timing.** `CurrentTime` (from
  `Croi_Time`), `DoubleClick` (fixed 500 ms v0), `DisplayBeep` (logs),
  `ReportMouse` (toggles `WFLG_REPORTMOUSE`+`IDCMP_MOUSEMOVE`). `SYS_*`
  94-97. Coverage **20%** (20 impl / 77 stub). **The modal requester
  (`AutoRequest`/`EasyRequestArgs`) is deferred** — its nested
  input-pump loop doesn't unit-test deterministically without the HID
  pump; it needs a dedicated harness (a `clar_smoke`-style pointer +
  injected click).
- **L5.5 shipped (`7fdf02b`): rendering helpers + gadget widen.**
  `struct Border`/`Image` (ABI); `IntuiTextLength`/`PrintIText`/
  `DrawBorder` (over the graphics.library impls); `AddGList`/`RemoveGList`/
  `OnGadget`/`OffGadget`/`RefreshGList`/`RefreshWindowFrame` (over the
  Leargas gadget substrate). `DrawImage` deferred (planar→chunky decode;
  left a pad). `SYS_*` 98-106. Coverage **29%** (29 impl / 68 stub).
- **Requester slice shipped (`c7ff55c`): `AutoRequest`/`EasyRequestArgs`.**
  `struct EasyStruct`; a modal-requester core (`Croi_Requester_Build` +
  `Croi_Requester_Wait`) over the window + bool-gadget + IDCMP substrate —
  Wait **blocks on the requester port** (`Croi_Wait`), so the pump (live)
  or a pre-posted GADGETUP (test) drives it, no self-drain race.
  `AutoRequest` (8 args) is a `local` marshalling stub (`req_stubs.c`,
  L4.4 pattern); `EasyRequestArgs` (4 args) is syscall (splits
  `es_GadgetFormat` on `|`). `SYS_*` 107-108. Coverage **31%** (31 impl /
  66 stub).
- **L5.6 shipped (`378555f`): screens — L5 EFFECTIVELY COMPLETE.**
  `OpenScreen`/`OpenScreenTagList`/`CloseScreen` + `struct NewScreen` +
  `SA_*` tags. v0 opens custom screens over the single boot display fb
  (registered via `Leargas_SetDisplayFramebuffer(&g_fb)` at boot); active
  save/restore via a new `LeargasScreen.prev_active`. `SYS_*` 109-111.
  Coverage **34%** (34 impl / 64 stub).
- **L5 — intuition.library: DONE** (L5.1 tag opener + window RPort, L5.2
  window ops, L5.3 menus, L5.4 feedback, L5.5 rendering + gadgets,
  requesters, L5.6 screens). The window system a V36 app drives is in.
  Tracked gaps: `DrawImage` (planar decode), the requester's live
  interactive loop is pump-driven (core tested via pre-post), v0
  no-occlusion/no-Layers, menu all-dropdowns + EasyRequest verbatim body,
  custom-screen no-repaint-on-close.
- **L6 — devices (Croi): COMPLETE (`docs/CROI_DEVICES.md`). L6.1–L6.4
  shipped (timer + console + input over the exec IO prims + CaraDevice
  registry).** Decisions locked there: (1) a device is a
  name-registered **`KOBJ_DEVICE`** (`struct CaraDevice` = exec `Device` +
  name + a kernel **`beginio` fn pointer** + open/close) in a small kernel
  registry (`Croi_Device_Register` at boot); `OpenDevice(name,…)` looks it
  up, fills `io_Device`. No classic negative device vec in v0 — `DoIO`
  calls `beginio` directly + an `io_Command` switch. (2) **Synchronous v0
  IO**: `DoIO` = `BeginIO`+return; `SendIO`→BeginIO+ReplyMsg, `WaitIO`/
  `CheckIO`/`AbortIO` degenerate; `TR_ADDREQUEST` is the one blocking
  command (over `Croi_Time`). (3) `timer.device` over `Croi_Time`
  (GETSYSTIME/ADDREQUEST); `console.device` `CMD_WRITE` → the L3.6 console;
  `input.device` minimal (IND_WRITEEVENT → Leargas ring; handler chain
  deferred). (4) all `syscall`, IORequests read SUM=1 (≤7 args, no stub).
  New `exec/io.h`. Slice plan: L6.1 IO prims + registry → L6.2 timer →
  L6.3 console → L6.4 input (minimal). LVO anchors: `OpenDevice -444` …
  `AbortIO -480` (slot into exec.conf pads).
- **L6.1 shipped (`b599c77`): exec IO primitives + device registry.**
  `exec/io.h`; `cara/device.h` + `src/croi/exec_lib/device.c` (the
  `CaraDevice` registry + the 7 IO verbs, synchronous v0). exec coverage
  **39%** (44 impl).
- **L6.2 shipped (`76700e7`): timer.device.** `devices/timer.h`
  (`struct timerequest`, `TR_*`/`UNIT_*`); `src/croi/device/timer_device.c`
  registered at boot via `Croi_Timer_Init()` (entry.c, after the dos
  handler). `TR_GETSYSTIME` reads `Croi_Time_Now` (normalised
  secs/micros); `TR_ADDREQUEST` waits a duration by cooperative
  poll-yield (like `Delay`); `TR_SETSYSTIME` accepted-but-ignored;
  `CMD_CLEAR` no-op; else `IOERR_NOCMD`. The registry now skips
  duplicate names so `Croi_Timer_Init` is idempotent (boot + self-test
  both call it). New `src/croi/device/` lib (`cara_devices`), linked
  normally into croi.
- **L6.3 shipped (`f74821f`): console.device.**
  `src/croi/device/console_device.c` registered at boot via
  `Croi_Console_Init()` (entry.c). `CMD_WRITE` emits `io_Data`/`io_Length`
  through the kernel log-backed `cout` sink — the same stream the dos
  console handler writes to (handler.c `console_write`), chunked to
  `CARA_LOG_MSG_LEN`. `CMD_READ` returns EOF (0 bytes, stub stdin like
  dos); `io_Actual` = bytes written; `CMD_CLEAR`/`CMD_FLUSH` no-op; else
  `IOERR_NOCMD`. CD_* specials deferred. No `.conf` change (device, not
  an LVO).
- **L6.4 shipped (`1448f13`): input.device — L6 COMPLETE.** New verbatim
  headers `devices/input.h` (`IND_*` command set) + `exec/interrupts.h`
  (`struct Interrupt`). `src/croi/device/input_device.c` registered at
  boot via `Croi_Input_Init()` (entry.c). `IND_WRITEEVENT` walks the
  `struct InputEvent` chain in `io_Data`, translates each
  (class/subclass/code/qualifier + X/Y → dx/dy, stamped via
  `Croi_Time_Now`) into a `LeargasInputEvent` and posts it into the ring
  (`Leargas_Input_Post`) — the same stream the HID pump feeds and the
  Leargas router drains; `io_Actual` = events posted.
  `IND_ADDHANDLER`/`IND_REMHANDLER` record the handler but do NOT invoke
  it (Leargas owns the ring; full handler chain deferred — §2.5). The
  `IND_SET*` tuning verbs are no-ops; else `IOERR_NOCMD`. No `.conf`
  change (device, not an LVO). L6 tracked gaps: async device IO
  (queues/device task), input handler chain not invoked, `CreateIORequest`/
  `DeleteIORequest` (amiga.lib, not exec), multi-unit, settable
  clock/EClock, the device tail (keyboard/serial/audio/trackdisk/
  clipboard + *.resource), keymap.library.
- **L7 — BOOPSI: SCOPED (`66f3944`, `docs/LEARGAS_BOOPSI.md`).** Read it
  before cutting more L7 code. Key decisions: object = instance data
  preceded by a hidden `struct _Object`; dispatch is `local` (U-mode,
  dispatchers are U-mode hooks); built-in `rootclass`; public class
  registry = kernel registry (`FindClass`/`AddClass`/`RemoveClass`
  syscall); `DoMethod`/`DoSuperMethod`/`CoerceMethod` in libcara.
- **L7.1 shipped (`188d118`): class/object core + rootclass.** Headers
  `<intuition/classusr.h>` + `<intuition/classes.h>` + `<clib/alib_protos.h>`.
  `NewObjectA -636`/`DisposeObject -642`/`MakeClass -678`/`FreeClass -714`
  (local, boopsi.c in .lib_text.intuition, shared-heap alloc via inlined
  ecalls); `FindClass -672` (syscall, kernel registry boopsi_registry.c,
  `SYS_FindClass=119`); rootclass built + registered at boot
  (`Croi_Boopsi_Init`). DoMethod/DoSuperMethod/CoerceMethod in libcara
  (libcara_boopsi.c). **64-bit fix: HOOKFUNC + CallHookPkt now return
  IPTR** (was ULONG) so OM_NEW's object pointer survives. RX-page
  gotchas: helpers force-inlined, `-fno-jump-tables` on boopsi.c,
  RootDispatch address taken via a `volatile` static initializer
  (absolute reloc). intuition coverage 33% (39 impl). Tested by
  extending `userintuition_smoke` (a custom rootclass subclass).
- **L7.2 shipped (`4e674a5`): attributes + object lists.** Four local
  LVOs (boopsi.c): `SetAttrsA -648`/`GetAttr -654` (OM_SET/OM_GET
  dispatch), `SetGadgetAttrsA -660` (OM_SET; gadget refresh deferred to
  L8), `NextObject -666` (steps an OM_ADDTAIL list). rootclass gained
  `OM_ADDTAIL`/`OM_REMOVE` (inline AddTail/Remove of the object's
  o_Node). `opGet.opg_Storage` + GetAttr storage are `IPTR*` (not
  ULONG*) for pointer-valued attrs. userintuition_smoke extended with
  the attr round-trip + a 3-object list walk. intuition coverage 37%
  (43 impl).
- **L7.3 shipped (`f4bec1f`): public class registry — L7 COMPLETE.**
  `AddClass -684`/`RemoveClass -708` as `syscall` over the L7.1 kernel
  registry (`SYS_AddClass=120`→`Croi_Boopsi_RegisterClass`,
  `SYS_RemoveClass=121`→`_UnregisterClass`); conf pad split at ord
  113-117 (-690..-702 stay padded for V36 pub-screen/draw-info).
  `NewObject`-by-name already resolved via FindClass — publishing was
  all that was missing. userintuition_smoke extended: by-name fails
  before AddClass, builds a real object after, fails again after
  RemoveClass. intuition coverage 39% (45 impl). **L7 done: machinery +
  rootclass; concrete system classes arrive with L8.**
- **L8 — gadtools.library: SCOPED (`5bbf845`, `docs/LEARGAS_GADTOOLS.md`).**
  Key decision: **gadtools-over-Leargas, not over-BOOPSI** (classic V36
  gadtools builds plain `struct Gadget`s — gadgetclass is a parallel
  mechanism), so L8 reuses the Leargas gadget substrate and is decoupled
  from concrete BOOPSI classes. New base-ful library in `src/croi/gadtools`,
  all `syscall`, shared-heap allocation. v0 kinds BUTTON/TEXT/NUMBER/
  CHECKBOX/CYCLE/MX/STRING/INTEGER; prop kinds (slider/scroller/listview)
  deferred to L8.5 (need router drag-tracking).
- **L8.1 shipped (`5b83be4`): library + render context.** gadtools.library
  constructed at boot; `<libraries/gadtools.h>` (GadToolsBase, *_KIND,
  NewGadget/NewMenu, GT*_*/NM_* tags) + `struct DrawInfo` + the *PEN
  indices in `<intuition/screens.h>`. `GetVisualInfoA -126`/`FreeVisualInfo
  -132` (shared-heap `struct CaraVisualInfo` = screen + default DrawInfo),
  `CreateContext -114`, `FreeGadgets -36`; `SYS_GT_*` 122-125. gadtools
  coverage 22% (4 impl). `KERNEL_TEST(gadtools_visualinfo)`.
- **L8.2 shipped (`fa70150`): CreateGadgetA + easy kinds.**
  `CreateGadgetA -30` + `GT_SetGadgetAttrsA -42` (syscall, SYS_GT_*
  126-127). CreateGadgetA allocs a shared-heap `struct Gadget` + a
  `struct GtGadgetExt` (label IntuiText + kind + NUMBER value/buf) on
  SpecialInfo, reads the kind's GT*_* tags via `Croi_GetTagData`
  (kernel-side), chains after prevGad. Kinds: BUTTON (RELVERIFY),
  CHECKBOX (GTCB_Checked→GFLG_SELECTED), TEXT (GTTX_Text), NUMBER
  (GTNM_Number formatted). GT_SetGadgetAttrsA applies post-create
  (absent tag → unchanged) + re-renders via `Leargas_Gadget_Render`.
  Rendering reuses the generic Leargas button look (bordered box +
  label); kind-specific visuals (checkmark, recessed bevel) come with
  `DrawBevelBoxA` in L8.3. gadtools coverage 33% (6 impl).
  `KERNEL_TEST(gadtools_creategadget)`.
- **L8.3 shipped (`3282201`): choice/edit kinds + bevel + attr read.**
  CYCLE/MX/STRING/INTEGER kinds, `DrawBevelBoxA -120`, `GT_GetGadgetAttrsA
  -138` (SYS_GT_* 128-129). GtGadgetExt restructured with `struct
  StringInfo` FIRST (offset 0) so STRING/INTEGER set GTYP_STRGADGET and
  `SpecialInfo == &ext->sinfo == ext` — the Leargas string editor reads
  it as a StringInfo, gadtools reads the same ptr as GtGadgetExt (one
  alloc/free). CYCLE/MX store the label array + active index, show
  labels[active] (click-advance is GT_GetIMsg L8.4; MX = single-field
  radio v0). DrawBevelBoxA draws shine/shadow edges via the graphics
  Move/Draw/SetAPen impls. GT_GetGadgetAttrsA reads attrs back into
  caller storage. gadtools coverage 42% (8 impl).
  `KERNEL_TEST(gadtools_kinds)`.
- **L8.4 shipped (`7298c24`): GT_GetIMsg + menu builder.** `GT_GetIMsg
  -72`/`GT_ReplyIMsg -78`/`GT_RefreshWindow -84`/`GT_BeginRefresh -90`/
  `GT_EndRefresh -96` + `CreateMenusA -48`/`FreeMenus -54`/
  `LayoutMenuItemsA -60`/`LayoutMenusA -66` (SYS_GT_* 130-138). GT_GetIMsg
  pops the IntuiMessage from the UserPort (Leargas IDCMP ring via
  `Croi_GetMsg`) + does the per-kind update (CYCLE/MX advance, CHECKBOX
  toggle), rewrites Code, re-renders; GT_ReplyIMsg disposes
  (Leargas_IDCMP_DisposeMsg). CreateMenusA walks NewMenu[] → L5.3
  Menu/MenuItem chain (item block carries its IntuiText); LayoutMenusA →
  Leargas_Menu_Layout off the VisualInfo screen. gadtools coverage 89%
  (17 impl; only GT_FilterIMsg/GT_PostFilterIMsg stubbed).
  `KERNEL_TEST(gadtools_imsg_menu)`.
- **L8.5 shipped (`c8511f2`): prop gadgets + router drag-tracking.** New
  `struct PropInfo` (intuition.h) + `src/croi/leargas/prop_gadget.c`
  (`Leargas_Prop_Render` container+knob, `Leargas_Prop_HandleDrag`
  pointer→pot); `Leargas_Gadget_Render` dispatches GTYP_PROPGADGET.
  **router.c drag-tracking** (the substrate piece): prop down-stroke jumps
  the knob to the click, button-held motion re-pots+re-renders, up-stroke
  posts GADGETUP. gadtools `SLIDER_KIND`/`SCROLLER_KIND` build a
  GTYP_PROPGADGET (GtGadgetExt's offset-0 SpecialInfo is now a
  `union{StringInfo;PropInfo}`); GTSL_/GTSC_ map to HorizPot/HorizBody.
  No new LVOs (kinds). `KERNEL_TEST(gadtools_prop)` incl. a full
  click-drag-release through `Leargas_Input_Drain`. **L8 gadtools
  COMPLETE** (only LISTVIEW/PALETTE deferred — LISTVIEW=scroller+rows,
  PALETTE=colour grid). **NEXT: L9 — asl.library** (the file/font/screen-
  mode requesters, built on gadtools). Needs a scoping doc first
  (`docs/?_ASL.md` — no Irish brand; likely `src/croi/asl`, all syscall
  over Leargas + the gadtools kinds). Then L10–14 long tail → T tools →
  A apps. L8 deferred: LISTVIEW/PALETTE kinds, GT_FilterIMsg/PostFilterIMsg,
  per-gadget fonts, IDCMP_MOUSEMOVE during drag (v0 commits on release).
- **L9 — asl.library: SCOPED (`a8b33c7`, `docs/LEARGAS_ASL.md`).** A thin
  composition over the L5 modal requester core + L8 gadtools kinds; new
  `src/croi/asl`, all syscall. v0 file requester is **string-entry** (no
  browse list — that needs the deferred gadtools LISTVIEW).
- **L9.1 shipped (`a8de180`): library + alloc/free.** asl.library built at
  boot; `<libraries/asl.h>` (FileRequester/FontRequester/
  ScreenModeRequester, ASL_*/ASLFR_*/ASLFO_*/ASLSM_*). `AllocAslRequest
  -48`/`FreeAslRequest -54`/`FreeFileRequest -36` (SYS_Asl_* 139-141).
  `struct CaraAslReq` = public union at offset 0 + type/config/path bufs;
  AllocAslRequest parses the initial-drawer/file/title/window tags. asl
  coverage 50% (3 impl). `KERNEL_TEST(asl_alloc)`.
- **L9.2 shipped (`b96d14f`): file requester.** `AslRequest -60` (file) +
  legacy `AllocFileRequest -30`/`RequestFile -42` (SYS_Asl_* 142-144).
  `Croi_Asl_Build` opens a centred modal window + two gadtools STRING
  gadgets (drawer/file) + OK/Cancel; `Croi_Asl_Wait` runs the modal loop
  (block on the window IDCMP port, OK id=1 / Cancel id=0, ignore string
  Returns) and on OK copies the edited buffers into rf_Dir/rf_File.
  Build/Wait split for the pre-post seam. **asl coverage 100% (6 impl).**
  `KERNEL_TEST(asl_filereq)`.
- **L9.3 shipped (`c445139`): font + screen-mode requesters — L9 COMPLETE.**
  `Croi_Asl_Build`/`Wait` now switch on req->type: font → a CYCLE over
  `g_asl_fonts` ({"topaz.font"}) → `fo_Attr` (ta_Name/ta_YSize 8);
  screen-mode → a TEXT label → `sm_DisplayID/Width/Height/Depth` from the
  screen. No new LVOs (the existing AslRequest path). `KERNEL_TEST
  (asl_fontmode)`. **L9 done** (file/font/screen-mode requesters over the
  L5 modal core + L8 gadtools; asl coverage 100%, 6 impl).
- **L10 — iffparse.library: SCOPED (`18a98d2`, `docs/IFFPARSE.md`).** The
  generic IFF chunk reader/writer (paint's ILBM; clipboard/datatypes
  build on it). New `src/croi/iffparse`, all syscall, parses kernel-side
  over a dos FileHandle (v0 `InitIFFasDOS` only — custom stream hooks +
  clipboard deferred).
- **L10.1 shipped (`fbb3997`): library + handle lifecycle.** AllocIFF -30/
  OpenIFF -36/CloseIFF -48/FreeIFF -54/InitIFFasDOS -222 (SYS_Iff_*
  145-149). `<libraries/iffparse.h>` (IFFHandle/ContextNode/codes/modes/
  IDs + GoodID/GoodType/IDtoStr inlines); `struct CaraIff` (public
  IFFHandle @0 + bound stream/mode + context stack). iffparse coverage
  14% (5 impl). `KERNEL_TEST(iffparse_lifecycle)`.
- **L10.2 shipped (`16d76cc`): the read walk.** `ParseIFF -42` (STEP/SCAN/
  RAWSTEP), `ReadChunkBytes -60`/`ReadChunkRecords -72`, `StopChunk -132`/
  `StopOnExit -144`, `CurrentChunk -162`/`ParentChunk -168` (SYS_Iff_*
  150-156). Walks big-endian chunks over the dos stream; SCAN stops at a
  StopChunk'd chunk; ReadChunkBytes clamps to the chunk. iffparse coverage
  34% (12 impl). Tested by extending **userexec_smoke** (a Process — a
  KERNEL_TEST can't open dos files): write a raw IFF FORM ILBM via dos,
  reopen, ParseIFF(SCAN) to BODY, ReadChunkBytes "hello!".
- **L10.3 shipped (`d4d275a`): the write side.** `PushChunk -84`/
  `PopChunk -90`, `WriteChunkBytes -66`/`WriteChunkRecords -78`
  (SYS_Iff_* 157-160). PushChunk writes the header (size placeholder +
  group type), PopChunk pads + backpatches the true size via Seek (CaraFS
  overwrites at offset) + accounts it against the FORM. `IFFSIZE_UNKNOWN`
  supported. userexec_smoke is now a full iffparse write→read round-trip.
  iffparse coverage 45% (16 impl).
- **L10.4 shipped (`8dd9182`): props/collections — closes L10.**
  `PropChunk -108`/`PropChunks -114`/`CollectionChunk -120`/
  `CollectionChunks -126`/`FindProp -150`/`FindCollection -156`
  (SYS_Iff_* 161-166). ParseIFF's SCAN loop calls a new `iff_gather()`
  after each `iff_step`: if the entered chunk's (type,id) was registered
  with PropChunk it slurps the whole body into a shared-heap
  StoredProperty (latest copy wins, FindProp returns it); if registered
  with CollectionChunk it prepends a CollectionItem (newest-first head,
  FindCollection). Gathered chunks are consumed transparently (SCAN keeps
  going) unless RAWSTEP suppresses gather. The gather registries live in
  CaraIff (`props[]`/`nprops`, `colls[]`/`ncolls`, both ≤ CARA_IFF_MAXSTOPS
  =8) and are reset in OpenIFF + freed in CloseIFF. EntryHandler -96/
  ExitHandler -102 + the 7 LocalContextItem LVOs (AllocLocalItem -174 …
  StoreItemInContext -210) stay **padded/stubbed** — no custom stream
  hooks in v0 and ILBM-class formats use PropChunk/FindProp directly; the
  local-item system was deemed out-of-scope substrate. userexec IFF
  round-trip now registers PropChunk(ILBM,BMHD) before SCAN and asserts
  FindProp returns the 4 BMHD bytes after stopping at BODY. iffparse
  coverage 62% (22/39).
- **L11 — icon.library: SCOPED (`38a97b4`, `docs/ICON.md`).** Two stacked
  pieces — a CaraFS xattr layer + the library — because icon metadata
  lives in an object's `cara.icon` xattr (CARAFS §3.10), not `.info`
  sidecars. Decisions: new code over the F4-frozen format (the
  `CARAFS_ITEM_XATTR` kind + `carafs_item_*` TLV helpers were already
  reserved); compact versioned `CaraIconBlob` (no binary `.info`, imagery
  deferred — file-manager draws a default glyph per `do_Type`);
  `FindToolType`/`MatchToolValue`/`BumpRevision` `local` flavour; the
  `XATTR_OVERFLOW` chain + legacy freelist API stubbed.
- **L11.1 shipped (`8dabe86`): xattr substrate + library + read path.**
  `Carafs_XattrGet/Set/Remove(m, cnode, name, …)` (`src/logaic/carafs/
  xattr.c`): one `CARAFS_ITEM_XATTR` item holds a packed record list
  `{u8 name_len; u8 flags; u16 val_len; name; val}`; Set closes the gap of
  any prior record in place then `carafs_item_resize`s and appends; inline
  only (`CARA_EOVERFLOW` past the cnode item budget). icon.library is the
  iffparse recipe: base+vec in the shared heap, `.lib_text.icon` RX
  trampolines, MakeLibrary in `entry.c`, whole-archived. `GetDiskObject`
  (`SYS_Icon` 167) Locks the path → reads the xattr → parses the
  `CaraIconBlob` into one shared-heap `struct DiskObject` (+ optional
  `DrawerData` + `ToolTypes` vector + strings); `FreeDiskObject` (168)
  frees it. New ABI headers `workbench/workbench.h` (DiskObject/DrawerData/
  `WB*`) + `workbench/icon.h` (IconBase). icon coverage 10% (2/19). The
  blob codec (`Croi_Icon_BlobBuild`/parse) + `Croi_Icon_ReadCnode(m,cnode)`
  are exposed for the test.
- **L11.2 shipped (`314d0d9`): write side + defaults.** `PutDiskObject
  -78`/`DeleteDiskObject -120` (serialise/remove the cara.icon xattr after
  a dos-Lock path resolution; both return BOOL), `GetDiskObjectNew -114`
  (Get, else a synthesised WBPROJECT default), `GetDefDiskObject -132`/
  `PutDefDiskObject -138` (per-type default stored on the volume root as
  the `cara.icondef<N>` xattr, else a built-in default) — `SYS_Icon`
  169-173. A built-in default reuses the codec (build a blob, parse it).
  The blob's drawer block is now keyed on a flag bit (`ICON_BLOB_F_DRAWER`)
  not `do_Type==WBDRAWER`, so WBDISK/WBGARBAGE defaults carry DrawerData
  too. Tested by a full `userexec` Gleas round-trip (Put→Get→assert
  fields→Delete→GetDiskObjectNew default→Put/GetDefDiskObject drawer;
  exit `0xBAE2`); persistence covered by the two-boot smoke. icon coverage
  36% (7/19).
- **L11.3 shipped (`80f476c`): tool-type/revision helpers — closes L11.**
  `FindToolType -90`/`MatchToolValue -96`/`BumpRevision -102`, `local`
  flavour: pure string ops in the `.lib_text.icon` RX page, JALR'd from
  U-mode with no trampoline (no sysno/syscall arm). `src/croi/icon/
  icon_strings.c` follows the RX-page discipline — self-contained, helpers
  always-inlined, no string literals (chars emitted directly so nothing
  lands in `.rodata` ~1 GiB away), `-fno-jump-tables` on the TU. userexec
  exercises all three. icon coverage 52% (10/19) — the full DiskObject +
  tool-type API is done; the legacy freelist LVOs (GetWBObject/GetIcon/
  AddFreeList/…) stay permanently stubbed (no modern caller). **L11 is
  CLOSED.**
- **L12 — diskfont.library: SCOPED (`3d03087`, `docs/DISKFONT.md`).** Two
  halves meeting at `struct TextFont`: a loader (new `src/croi/diskfont`)
  + a Dath rendering change. Decisions: a CaraOS-native **Cara font file**
  (flat serialised classic bitmap strike — no 68k hunk); render from a
  generic `TextFont` strike (topaz reframed); fonts under `FONTS:` read via
  dos; syscall flavour; outline/colour/scaled fonts + a unified
  OpenFont/OpenDiskFont font list deferred.
- **L12.1 shipped (`ffb6b0a`): Dath renders from a generic strike.**
  graphics.library `Text`/`TextLength` (`src/croi/dath/graphics_lib.c`) now
  render/measure from the bound `rp->Font` strike — `tf_CharData`
  (horizontal strike, `tf_Modulo` bytes/row), `tf_CharLoc` =
  `(bitOffset<<16)|bitWidth`, default-glyph fallback, advance by
  `tf_CharSpace` (proportional) else `tf_XSize` — not the hardwired
  `dath_font_8x8`. The built-in topaz is reframed as a strike
  (`gfx_build_system_strike` packs the row-major-per-glyph 8x8 into a
  horizontal strip; `tf_CharData`/`tf_CharLoc` point at kernel statics,
  read S-mode, apps never deref). A pure refactor —
  `KERNEL_TEST(graphics_text_strike)` renders 'A' and compares every pixel
  to the source glyph. No new LVOs.
- **L12.2 shipped (`895da18`): diskfont.library + OpenDiskFont.** A
  base-ful `syscall` library (`src/croi/diskfont`, the icon recipe).
  `OpenDiskFont -30` (`SYS_Diskfont` 174) derives `FONTS:<name>/<ysize>`
  from the TextAttr (strips `.font`; `FONTS:` → volume root in v0), reads
  via the dos bridge, and parses a **Cara font file** (flat serialised
  classic strike, `cara/diskfont_lib.h §2.2`) into one shared-heap
  `TextFont` (TextFont + strike + charLoc[+space/kern]) marked
  `FPF_DISKFONT`; Dath (L12.1) renders it. `graphics.library CloseFont`
  now frees a disk font. `Croi_Diskfont_ParseFont` is the dos-free parse
  seam. New ABI header `libraries/diskfont.h` (FontContents/
  FontContentsHeader/AvailFonts/AvailFontsHeader/DiskFontHeader + FCH_ID/
  AFF_*/MAXFONTPATH + DiskfontBase). diskfont coverage 20% (1/5). Tested by
  `KERNEL_TEST(diskfont_render)` (parse + render a hand-built box glyph,
  pixel-compare, no dos) + a userexec Gleas (write to `FONTS:test/8` →
  OpenDiskFont → verify metrics + FPF_DISKFONT + strike first byte; exit
  `0xBAE3`).
- **L12.3 shipped (`88b2e25`): AvailFonts + FontContents helpers — closes
  L12.** `AvailFonts -36`/`NewFontContents -42`/`DisposeFontContents -48`
  (`SYS_Diskfont` 175-177); `NewScaledDiskFont -54` stub. AvailFonts packs
  an `AvailFontsHeader` + `AvailFonts` records (ta_Name into the caller's
  buffer) for the ROM face (AFF_MEMORY) + each disk font under `:Fonts`
  (AFF_DISK), returns 0 if it fit else extra bytes needed; NewFontContents
  builds a `FontContents` index from a family's size files. **Enumeration
  walks CaraFS directly (`g_carafs`)** so it needs no Process —
  KERNEL_TEST-able. **Font storage convention settled: `root/Fonts/<family>/
  <ysize>`; OpenDiskFont now resolves `:Fonts/<name>/<ysize>`** (FONTS: has
  no dos assign in v0) — the L12.2 Gleas updated to match. diskfont 80%
  (4/5). Tests: `KERNEL_TEST(diskfont_avail)` (seed via CaraFS, assert ROM
  + disk faces, small-buffer byte-count, NewFontContents) + the userexec
  Gleas calls AvailFonts. **Deferred (tracked):** wiring the asl font
  requester's CYCLE list to AvailFonts (cross-library; the requester keeps
  its single face until paint/file-manager drives font selection). **L12
  CLOSED.**
- **L13 — commodities.library: SCOPED (`bdb3c99`, `docs/COMMODITIES.md`).**
  First long-tail lib with **no app consumer**; its live half (filter live
  InputEvents through a CxObj tree, route CxMsgs) is blocked on the
  input.device handler chain (recorded-not-invoked, `CROI_DEVICES §2.5`).
  Decision: ship the testable app-independent core (ABI + CxObj object model
  + CxMsg accessors + ParseIX), defer live input dispatch.
- **L13.1 shipped (`33fb3ab`): library + the CxObj object model.** The
  contiguous `-30..-102` lifecycle/tree block (`SYS_Cx` 178-190):
  CreateCxObj/CxBroker, ActivateCxObj, DeleteCxObj/DeleteCxObjAll,
  CxObjType, CxObjError/ClearCxObjError, SetCxObjPri, AttachCxObj/
  EnqueueCxObj/InsertCxObj/RemoveCxObj. A commodity builds a tree of CxObjs
  (broker root + children) on the shared heap; the opaque public `CxObj *`
  IS the kernel-private `CaraCxObj` (children singly-linked via `next`;
  DeleteCxObj unlinks + frees the subtree). New ABI header
  `libraries/commodities.h` (CxObj/CxMsg opaque, NewBroker, InputXpression,
  CX_*/CXM_*/COERR_*/base CxBase). icon recipe; no live input.
  `KERNEL_TEST(commodities_tree)` builds/queries/tears down a tree + the
  null-safety contract. commodities coverage 72% (13/22). **NEXT: L13.2**
  (closes L13) — `SetTranslate -108`/`SetFilter -114`/`SetFilterIX -120`
  (store config on the CxObj) + `ParseIX -126` (input-description string →
  `InputXpression`, the pure parser) + the CxMsg accessors (`CxMsgType`/
  `CxMsgData`/`CxMsgID`/`DisposeCxMsg`/`RouteCxMsg`, offsets locked against
  `commodities_lib.fd`); `AddIEvents`/`InvertKeyMap` stay stubbed.
  KERNEL_TEST: `ParseIX("ctrl alt f1")` → assert the IX fields. After L13:
  L14 = expansion.library (FDT-backed AutoConfig analogue). Then T tools →
  A apps (`docs/PORTING.md` + editor/paint/file-manager — now have icon +
  disk-font backing). **Deferred (tracked):** the input-handler chain — the
  load-bearing substrate commodities' live half + intuition-as-handler both
  need; arrives when a commodity T-tool needs live hotkeys. Old L10.4 note
  kept below.

  (Superseded note) After L10:
  L11-14 = icon.library (.info ↔ CARAFS cara.icon
  xattr §3.10; file-manager), diskfont, commodities, expansion — scope
  each. Then T tools → A apps. Standing deferred substrate an app may
  force forward: gadtools LISTVIEW (→ asl dir browse), layers.library
  (window occlusion — windows non-overlapping today), DrawImage
  (planar→chunky), async device IO, the input handler chain.
  After L8: L9 asl → L10–14 long tail → T tools → A apps. L7 tracked
  gaps: concrete BOOPSI classes (gadgetclass/imageclass/icclass/
  modelclass), `DoGadgetMethodA`, `OM_NOTIFY`, FreeClass reaping,
  `DrawImageState` (gated on deferred `DrawImage`).
- **Rich gadgets (list/cycle/slider/…) are gadtools (L8) on BOOPSI (L7);
  the file requester is asl (L9)** — not L5. L5 is the window-system core
  + menus + requesters.
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
