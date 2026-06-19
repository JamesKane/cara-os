# CaraOS session handoff — 2026-06-11

> A pick-up-where-we-left-off note for a fresh session. Captures
> current state, the non-obvious decisions from the last sprint, the
> concrete next steps, and the build/test/commit workflow. Pairs with
> `docs/ROADMAP.md` (the phase plan), `docs/CARAFS.md` (the filesystem
> design — the current work), and `docs/ARCHITECTURE.md` (the system
> design).

---

## 1. Where we are

**Phase 1 shipped** (see §5 for the live demo recipe). **Phase 2 is in
flight**: the NVMe driver and the CaraFS core through epic F4 are
done; F5 (kernel mount over NVMe) is next. **The on-disk format is now
frozen** (F4 boundary) — bump `incompat` for any later change.

Recent commits (newest first), all on `main`:

```
d941883 phase-2/F4    CaraFS journal — WAL, ordered data, replay, crash test
0af45f8 phase-2/F3    CaraFS directories — inline→leaf→tree, links, scale
591d1d3 phase-2/F2    CaraFS cnodes, files, allocator
a286aa0 phase-2/F1    CaraFS bdev + cache + mkfs/fsck v0
16b5265 phase-2/F0    CaraFS format foundation
b0463ae docs          CaraFS design (Phase 2 Subgoal 2)
5cb4c0c phase-2/N1-N5 NVMe driver — probe to write/readback under QEMU
```

Status: everything green — host ctest **26/26**, in-kernel tests
**25 passed / 0 failed**, QEMU boot smoke ok, `format-check` clean.
(The host suite runs ~20–25 s: `test_carafs_dir`'s scale test and
`test_carafs_journal`'s crash-injection harness dominate.)

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

## 3. What's next: F5 — kernel mount (then F6)

### F4 shipped (`d941883`) — journal. **Format frozen.**

The metadata WAL is live; the internals a fresh session needs:

- **`journal.c` is the WAL.** A commit (`carafs_txn_commit`, in
  `cache.c`) now: (1) flushes dirty file-data home (ordered-data
  barrier), (2) `carafs_journal_append` writes one txn —
  `DESC | image[] | COMMIT` — to the circular log and flushes (the
  durability point), (3) releases TXN pinning, leaving images dirty for
  **lazy** home writeback, (4) opportunistically checkpoints. Home
  writes therefore lag the log; `carafs_journal_checkpoint`
  (`carafs_cache_sync` + advance the JSB past the whole log) is the only
  thing that makes them durable + reclaims log space. Sync/Unmount
  checkpoint; Unmount then clears DIRTY.
- **Replay** (`carafs_journal_replay`, on a DIRTY RW mount) applies
  every complete txn home in order — validated by per-block CRCs, a
  chained image CRC, and a contiguous seq — and stops at the first torn
  txn. Then it reloads the recovered superblock and marks the volume
  clean. Bounded by journal size. (A read-only mount of a DIRTY volume
  skips replay → stale view; documented dev/disaster path.)
- **Log geometry.** Offsets `1..journal_blocks-1`, physical block =
  `journal_start + offset`; txns may wrap (all addressing via `j_adv`).
  `CARAFS_TXN_MAX_BLOCKS` is now **60** (DESC target list must fit a
  512 B block, static_asserted); geometry floors the log above one
  maximal txn. Two arena-carved scratch blocks (`j_scratch`, `j_image`)
  build records / read images — the core still never allocates.
- **Crash-injection harness** (`test_carafs_journal.c`, §4): a recording
  bdev captures every block write of a workload; then *every prefix* of
  that stream is replayed into a fresh image and checked (fsck clean +
  no torn objects), with the full stream recovering the complete state.
  This is the strongest correctness lever in the FS — extend the
  workload there when touching commit/replay.
- **Still open** (fair game in F5+): full recursive fsck with
  link-count reconciliation; the collision/`seq` path remains
  test-by-construction only (FNV-1a-64 collisions can't be forged).

### F5 — kernel mount (the current epic)

`Croi_Nvme_Flush` (NVMe Flush command; the admin path exists), a
`CarafsBdev` over `Croi_Nvme_*` (one FS block = block_size/512 LBAs),
mount at boot when a CaraFS superblock is found, `KERNEL_TEST(carafs_mount)`
+ a write/reboot-persist smoke stage. The smoke harness already
provisions an NVMe scratch image. The CarafsBdev `flush` must map to the
new `Croi_Nvme_Flush` so the WAL's ordering/durability holds on real
hardware. Then **F6** hands off to Subgoal 3 (GPT/UUID discovery, root
volume selection, Startup-Sequence) in the Logaic boot-path doc.

Phase 2's success criterion: Clar's drawer is a CaraFS directory
listing (replacing the hard-coded Bosca), edit → write → reboot →
still there.

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
