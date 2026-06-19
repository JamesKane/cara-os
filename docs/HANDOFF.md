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
**31 passed / 0 failed**, QEMU boot smoke ok (two boots: partition +
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
- **Slice 4+ (next):** `FindTask(NULL)` (self, via `Sched_Current()`),
  semaphores (`ObtainSemaphore`/`ReleaseSemaphore`/…), libraries
  (`MakeLibrary`/`SetFunction`/`SumLibrary`), and the `OpenDevice`/
  `DoIO`/`SendIO`/`CheckIO`/`WaitIO`/`AbortIO` device primitives (L6
  builds on them). Kernel-state → syscall; pure → local. Then the
  **stub-coverage report** + widening the conf toward the full Exec
  autodoc (still `##pad_run`s).
- **Still TODO in L1:** the **stub-coverage report** (PHASE3.md §7 Q4)
  — a check listing which declared LVOs are unimplemented stubs. Not
  built yet; settle its format as the per-library precedent. The conf
  is *not* yet the full Exec autodoc (still `##pad_run`s); widening to
  ABI-complete is ongoing.

Open questions for L1/L3 in PHASE3.md §7: BSTR/BPTR site, Process-vs-Task
layout, `proto/` search path, stub-coverage report, which sample apps.

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
