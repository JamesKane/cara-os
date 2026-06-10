# CaraFS — a modernised FFS for CaraOS

> The Phase 2 Subgoal 2 design document (`docs/ROADMAP.md`). CaraFS is
> the native CaraOS filesystem: the *flavour* of the AmigaOS Fast File
> System, redesigned for NVMe-era storage. This doc records the on-disk
> format design and its invariants, the software architecture, and the
> epic breakdown; the authoritative byte layout will live in the format
> header (`include/cara/carafs.h`, epic F0) with host unit tests
> asserting every offset. Pairs with `docs/PHASE2_NVME.md` (the block
> driver underneath) and, eventually, the Logaic boot-path plan
> (Subgoal 3, its own doc).
>
> FFS semantics are cited from the AmigaDOS Manual 3rd Edition
> (`amiga_docs/1991-baker-jesup-et-al-the-amigados-manual-3rd-ed.pdf`)
> and the V36+ `dos.library` autodocs. Cleanroom rule applies
> (PRINCIPLES §2): we read the spec of FFS to *honour* it, and we read
> no third-party filesystem code at all.
>
> **Decided 2026-06-10: crash consistency is metadata journaling**
> (write-ahead log + ordered data), not copy-on-write. Rationale in §3.9.

---

## 1. Goals and non-goals

From ROADMAP Phase 2 Subgoal 2, made falsifiable:

1. **64-bit block addressing.** Volumes to 2^64 blocks; files to 2^63
   bytes. No 4 GiB anythings.
2. **Directories that scale.** A million-entry drawer lists and looks
   up without degrading — FFS hash chains, reimagined as a hash-keyed
   tree (§3.6). A host unit test creates 1M entries and asserts
   lookup cost stays logarithmic (block reads counted, not wall time).
3. **Crash consistency by journal replay.** Pull the plug mid-write;
   the volume mounts clean with no validator scan (§3.9). The FFS disk
   validator's job — and its multi-minute full-disk walk — is replaced
   by bounded WAL replay.
4. **Hard links** (and soft links) as first-class cnode features, not
   FFS's bolted-on link chains.
5. **Endianness-clean**: every on-disk integer is **little-endian** by
   definition, accessed through explicit codec helpers. FFS was
   incidentally big-endian; we are deliberately LE (RV64 native), and
   the format is still well-defined if a big-endian host ever runs the
   tools.
6. **Extended attributes** sized for the `.info` icon-metadata pattern:
   Workbench icon state rides *on the object itself* instead of a
   sibling `.info` file (§3.10).
7. **Amiga semantic parity** where user programs can see it:
   `fib_Protection` bits stored verbatim, 79+ char file comments,
   DateStamp-exact timestamps, case-insensitive/case-preserving names
   (§5).

**Non-goals for Phase 2** (ROADMAP "out of scope" + consequences of
the journaling decision): network filesystems; RAID/LVM/encryption;
snapshots (a CoW feature — we chose otherwise); quotas, UIDs, ACLs
(one user is god, PRINCIPLES §0); compression; online resize; FAT
migration (the SD card stays the fallback boot route).

---

## 2. What "the flavour of FFS" means

What we keep, what we modernise, what we drop — so the word "flavour"
is a checklist, not a vibe.

**Kept (the soul):**

| FFS concept | CaraFS form |
|---|---|
| One header block per object, anywhere on disk; the object's identity *is* its block number | The **cnode**: one block per object, `cnode id == block number` (§3.4). No inode table to size at mkfs, no inode exhaustion |
| Root block describes the volume; volume has a name and is renameable | Superblock + root directory cnode; volume name + UUID in the superblock (§3.3) |
| Hashed directory lookup (FFS: 72-slot table, `hash = (hash*13 + toupper(c)) & 0x7FF`, chains) | Hash-keyed directory tree, 64-bit folded-name hash, scalable to millions of entries (§3.5/§3.6) |
| Bitmap free-space accounting (FFS bitmap blocks off the root) | Per-allocation-group bitmap blocks (§3.8) |
| Object name and comment stored in the header block itself | NAME / COMMENT items in the cnode's item area (§3.4) |
| `fib_Protection` bits (`hsparwed`), DateStamps | Stored verbatim / losslessly convertible (§5) |
| Case-insensitive, case-preserving names | Kept, with a defined fold (§3.5) |
| The disk validator restores consistency after a crash | Journal replay does, in bounded time (§3.9) |

**Modernised:** 512-byte fixed blocks → 4 KiB default (§3.1); 72
data-block pointers + extension-block chains → extents + an extent
tree (§3.7); BCPL strings → `(len, bytes)` UTF-8; 30-char names → 255
bytes; big-endian → little-endian; no checksums → CRC-32C on every
metadata block; link objects chained off the target → reference-counted
hard links (§3.11).

**Dropped:** OFS data-block headers (FFS already dropped them);
internationalisation modes as *format variants* (FFS had OFS/FFS ×
INTL × DIRCACHE — CaraFS has exactly one format with feature-flag
room, §3.3); the validator.

---

## 3. On-disk format

### 3.1 Global invariants

- **Byte order:** little-endian, always, accessed via `carafs_le*`
  codec helpers — never by struct-cast on the host side; the kernel
  side may cast (RV64 is LE) but only through the same header types.
- **Block size:** power of two, 512 B – 64 KiB; **4096 default**
  (matches the page allocator, the NVMe driver's PRP path, and 512e/4Kn
  disks). One volume has one block size, fixed at mkfs.
- **Checksums:** every metadata block (superblock, cnode, tree node,
  bitmap, journal record) carries CRC-32C over the whole block with
  the checksum field zeroed during computation. Data blocks are not
  checksummed in Phase 2 (feature-flag room left).
- **Self-identification:** every metadata block opens with a 4-byte
  magic naming its kind and the u64 block number it believes it lives
  at — fsck's two cheapest, highest-yield questions.
- **Time:** u64 nanoseconds since **1978-01-01T00:00:00 UTC — the
  Amiga epoch**. Converts exactly to/from `struct DateStamp` (1 tick =
  20 ms = 20,000,000 ns); spans to year ~2562.
- **Generations:** cnodes carry a u64 generation bumped on free/reuse,
  so a stale reference (dangling Lock, future NFS-style handle) is
  detectable — the same use-after-close discipline as kernel Handles
  (ARCHITECTURE §5).

### 3.2 Volume layout

```
LBA-space (one GPT partition, discovered by Logaic in Subgoal 3):

  block 0            superblock
  block 1 .. J       journal region (J = mkfs-sized, §3.9)
  block J+1 ..       allocation groups, back to back:
                       [ AG header+bitmap block | data blocks ... ]
  last block         backup superblock (written at mkfs + relabel only)
```

Everything else — cnodes, directory tree nodes, extent tree nodes,
xattr overflow, file data — is an ordinary allocatable block inside
some allocation group. There are no fixed metadata regions to outgrow,
which is the FFS "header blocks live anywhere" property doing modern
work.

### 3.3 Superblock

Fields (order/size recorded authoritatively in `carafs.h`):

- magic (`"CaraFS!\n"`, 8 bytes), format version u32
- **feature masks**: `compat`, `ro_compat`, `incompat` u32 each — the
  standard trick so an old driver refuses (or mounts read-only) a
  volume using features it doesn't know
- block_size_log2, total_blocks u64
- journal_start u64, journal_blocks u32, journal_seq u64
- ag_size_blocks u32, ag_count u32
- root_cnode u64, free_blocks u64 (advisory; fsck recomputes)
- volume UUID (16 bytes), volume name (u8 len + ≤63 bytes UTF-8)
- created_ns, modified_ns
- **state**: `CLEAN` / `DIRTY` — set DIRTY on first mounted write,
  CLEAN on unmount; a DIRTY mount triggers journal replay, never a
  full scan
- backup_sb_block u64, crc32c

### 3.4 Cnode

One block. `cnode id == block number`. Fixed header, then a **TLV item
area** filling the rest of the block:

Header: magic `CNOD`, crc32c, own block number, generation, type
(FILE / DIR / SYMLINK), link_count u32, size_bytes u64, blocks_used
u64, parent_cnode u64 (§3.11), created/modified/changed ns, 
fib_protection u32 (verbatim V36+ bits, §5), flags u32,
tree_root u64 (FILE: extent tree; DIR: directory tree; 0 = everything
is inline), n_inline_extents + inline extent array (FILE only,
16 × 16 B; §3.7).

Item area (TLV: u16 kind, u16 len, payload, 8-byte aligned):

- `NAME` — the object's own name (FFS stored it in the header block;
  so do we — fsck can reconstruct directories from cnodes alone)
- `COMMENT` — the AmigaDOS file comment, ≤ 255 bytes (V36 needs 79)
- `XATTR` — (name len, name, value) pairs, e.g. `cara.icon` (§3.10)
- `INLINE_DATA` — file contents for tiny files (≲ 3 KiB at 4 KiB
  blocks): a config file costs one block total, no extent
- `INLINE_DIRENTS` — small directories live entirely in their cnode
- `SYMLINK_TARGET` — soft-link path
- `XATTR_OVERFLOW` — u64 pointer to a chain of dedicated xattr blocks
  when the inline area overflows

A 4 KiB cnode block leaves ~3.4 KiB of item room after the header —
generous for name + comment + icon metadata + 16 extents.

### 3.5 Names, hashing, case rules

- Names are UTF-8, 1–255 bytes, may not contain `/`, `:` (AmigaDOS
  path syntax, owned by Logaic) or NUL. Case-**preserving**.
- Lookup is case-**insensitive** under an **ASCII-only fold** (`A–Z`
  → `a–z`, applied bytewise — safe on UTF-8 multibyte sequences).
  FFS INTL mode also folded Latin-1 `À–Þ`; we deliberately don't:
  folding high bytes inside UTF-8 corrupts, and full Unicode folding
  drags in locale tables forever (rejected — bloat + instability).
  Divergence documented here, revisit only with a real user need.
- **Name hash** (directory key): FNV-1a 64 over the folded bytes.
  Adversarial collisions are a non-concern (one trusted user); for
  honest input the 64-bit space keeps million-entry directories
  essentially collision-free, and §3.6 handles equal hashes anyway.
  This is the moral successor of FFS's `hash*13` — same role, 53 more
  bits.

### 3.6 Directories

Three sizes, transparently promoted:

1. **Inline**: entries live as `INLINE_DIRENTS` in the cnode until the
   item area fills (~40–60 typical entries). Most drawers never leave
   this stage; listing a small directory reads one block.
2. **Single leaf**: one dedicated dirent block.
3. **Tree**: a B+tree keyed by `(name_hash u64, collision_seq u8)`.
   Interior nodes hold ~250 `(key, child)` pairs per 4 KiB block;
   leaves hold dirents sorted by key (~100 typical-name entries per
   leaf). A million entries ≈ 2 interior levels + ~10k leaves:
   lookup = 3–4 block reads, insert/delete =
   one leaf rewrite + rare splits. Equal-hash names get ascending
   `collision_seq`; lookups compare folded names within the run.

Dirent: `(name_hash u64, cnode u64, type u8, name_len u8, name,
pad-to-8)`. The type byte spares a cnode read during listing —
`ExNext()` wants `fib_DirEntryType` cheaply.

Iteration order is key order. `ExNext()` resumes from a `(hash, seq)`
cursor: insertions/deletions elsewhere in the directory never skip or
repeat surviving entries — the V36 contract a FIB-chain walker
actually relies on.

The same B+tree machinery (node format, split/merge, cursor) is shared
with the extent tree (§3.7) — one implementation, two key types.

### 3.7 Extents and the extent tree

- Extent record (16 B): `start_block u64, count u32, flags u16
  (UNWRITTEN for preallocation), rsvd u16`. Max 2^32 blocks ≈ 16 TiB
  per extent at 4 KiB.
- A file's first 16 extents live in the cnode header. NVMe + bitmap
  locality means almost every file is 1–2 extents; 16 covers
  pathological append patterns before spilling.
- Past 16, extents move to a B+tree keyed by file block offset
  (`tree_root` in the cnode).
- Sparse files: absent ranges read as zeroes; `UNWRITTEN` extents too.

### 3.8 Free space — allocation groups

- AG = **8 × (block_size − 64) blocks** — 32,256 blocks ≈ 126 MiB at
  4 KiB — so the whole AG's bitmap plus a 64-byte self-identifying
  header (magic, crc32c, own block number, exact free count, rotor
  cursor) fills exactly the one block leading the AG.
- Allocator policy (all advisory, not format): new directories spread
  round-robin across AGs; file blocks allocate from the parent
  directory's AG with a per-AG rotor cursor; extents grab the longest
  free run ≤ requested from the rotor forward. Simple, local, and the
  16-inline-extent budget is sized for its fragmentation behaviour.
- Bitmap blocks are metadata: journaled (§3.9). `free_blocks` in the
  superblock is advisory; per-AG free counts live in the AG bitmap
  block header and are exact.

### 3.9 Journal

**Why journaling, not CoW** (decision record): in-place update keeps
`cnode id == block number` (the FFS identity property §3.4) with no
indirection map; memory stays bounded (PRINCIPLES §4: kernel +
services ≤ 128 MiB, hard); the implementation is a fraction of CoW's
(no refcounts, no deadlists, no map-of-the-map); and "replay the WAL"
is the honest modern descendant of "run the validator". We give up
free snapshots — acknowledged, not needed for Phase 2's success
criterion.

Mechanics — physical-block metadata WAL, circular over the journal
region:

```
transaction := DESC block | metadata block images ... | COMMIT block
DESC   : magic, seq u64, n_blocks u32, target block numbers[]
COMMIT : magic, seq, crc32c over the whole transaction
```

- **What's journaled:** superblock, cnodes, directory/extent tree
  nodes, AG bitmap blocks, xattr overflow blocks. **Never file data.**
- **Ordering rule (data=ordered):** data extents referenced by a
  transaction are written *and flushed* (NVMe Flush) before the
  COMMIT block is written, then the commit is flushed. A crash shows
  either the old metadata or new metadata pointing at fully-written
  data — never metadata pointing at garbage.
- **Checkpoint:** committed images are written home lazily; the
  journal tail advances after home blocks flush. Checkpoint pressure
  = journal ~½ full or 5 s idle.
- **Replay:** scan from the superblock's `journal_seq`, validate each
  transaction's CRC, write the images of every *complete* transaction
  home in order, discard the first incomplete one, flush, mark CLEAN.
  Bounded by journal size, not volume size.
- **Sizing:** mkfs sets `clamp(volume/64, 1 MiB, 64 MiB)` — the
  1 MiB floor keeps the 16 MiB QEMU scratch volumes viable.
- Prerequisite noted: the NVMe driver needs `Croi_Nvme_Flush` (opcode
  0x00 was defined in N1 but no API wraps it) — first task of epic F5.

### 3.10 Extended attributes and the `.info` pattern

Classic Workbench kept icon metadata in a sibling `.info` file —
a second directory entry, a second header block, orphanable. CaraFS
stores it *on the object*: the `cara.icon` xattr holds what
`icon.library` (Phase 3) will parse — icon imagery reference, drawer
window geometry, tool types. Namespaced names (`cara.*` reserved,
`user.*` free-form); value ≤ 64 KiB (inline first, then
`XATTR_OVERFLOW` chain). Whether `dos.library` *also* synthesises
virtual `.info` entries for V36 programs that `Open("foo.info")`
directly is a Phase 3 `icon.library`/Logaic decision — the format
just has to make the data reachable, and does.

### 3.11 Links

- **Hard links:** `link_count` on the cnode; dirents from anywhere
  point at it. Directories: exactly one parent (no directory hard
  links), so a directory's `parent_cnode` is authoritative — FFS kept
  a parent pointer and so do we, it makes fsck and `ParentDir()`
  trivial. For files, `parent_cnode` is meaningful only while
  `link_count == 1` (set to 0 on the second link). FFS modelled hard
  links as chained `ST_LINKFILE` objects hanging off the target;
  reference counting replaces the chain.
- **Soft links:** SYMLINK cnode, target path in `SYMLINK_TARGET`
  item. Resolution is Logaic's job (paths, assigns, `:` syntax);
  CaraFS just stores bytes.

### 3.12 Limits

| Thing | Limit |
|---|---|
| Volume | 2^64 blocks (16 EiB at 4 KiB before LBA math runs out) |
| File | 2^63 − 1 bytes |
| Name | 255 bytes UTF-8 |
| Comment | 255 bytes (V36 parity needs 79) |
| Directory entries | bounded by tree depth — practically unbounded; 10^6 is the tested point |
| Hard links per cnode | 2^32 − 1 |
| Xattr value | 64 KiB |
| Timestamps | 1978 + ~584 years, 1 ns resolution |

---

## 4. Software architecture

**The core is portable, pure logic** — the FDT-parser pattern
(CLAUDE.md testing layers), because that's where this design gets its
testability:

```
src/logaic/carafs/          cara_carafs static lib, host + riscv64
  format.c                  codecs, checksums, layout asserts
  bdev.h                    the seam: struct CarafsBdev
  cache.c                   block cache (write-back, pinning)
  alloc.c                   AG bitmaps, extent allocation
  cnode.c                   cnode + item area
  btree.c                   shared B+tree (dir + extent flavours)
  dir.c  file.c  journal.c  mount.c

include/cara/carafs.h       on-disk format: the authoritative bytes
tools/carafs/               mkfs.carafs, fsck.carafs (host, link the core)
tests/unit/test_carafs_*.c  host tests: image files via host CarafsBdev
src/croi/.../               kernel binding: CarafsBdev over Croi_Nvme_*
```

- **The bdev seam** is read/write/flush callbacks + block geometry.
  Host tools and unit tests bind it to a plain file; the kernel binds
  it to the NVMe driver. The core never includes a kernel header that
  doesn't exist on the host (it gets `cara/types.h` treatment).
- **Block cache** inside the core: fixed block count set at mount
  (kernel: ~256 × 4 KiB = 1 MiB, inside the 128 MiB budget; host
  tools: more). Journal commit and the ordering rule of §3.9 are
  implemented as cache flush disciplines, so they're host-testable.
- **Crash-injection testing** is the payoff: a host harness wraps the
  file-backed bdev, records every write, and replays arbitrary
  prefixes of the write stream into a copy of the image — simulating
  power loss at *every write boundary* of a workload — then mounts
  and asserts: replay succeeds, fsck is clean, and ordered-data
  guarantees hold. This is the test FFS never had and the reason the
  core must not touch real hardware directly.
- **Concurrency, Phase 2:** one mounter, coarse per-volume lock at
  the API boundary. The long-term home of this core is an AmigaDOS
  **handler** — a Logaic server Gleas receiving DosPackets (the
  `server` LVO flavour, `docs/LVO.md`) — which is also the classic
  architecture. In-kernel direct calls are the Phase 2 stand-in;
  nothing in the core may assume kernel context.

---

## 5. AmigaDOS semantic parity notes

- **Protection bits:** `fib_Protection` stored verbatim, including
  the inverted sense of the low nibble (`d/e/w/r` bit set = action
  *denied*) and `a/p/s/h`. CaraFS neither interprets nor enforces
  them (one user is god); it round-trips them for
  `SetProtection()`/`Examine()` parity.
- **DateStamps:** `ds_Days/ds_Minute/ds_Tick` ↔ Amiga-epoch
  nanoseconds, exactly (tick = 20 ms). `SetFileDate()` survives a
  round trip bit-for-bit.
- **Comments:** `SetComment()`'s 79 chars fit the 255-byte COMMENT
  item; stored as written.
- **`ExNext()`** iteration: stable under concurrent mutation per
  §3.6's cursor rule.
- **Volume identity:** `Relabel()` rewrites the superblock name (and
  the backup superblock); the UUID never changes after mkfs — Logaic's
  GPT-based mount (Subgoal 3) keys on it.
- **Case rules:** `Lock("WORK:FooBar")` finds `foobar` — §3.5 fold.
- **Validation:** there is deliberately no `DiskDoctor` analogue;
  `fsck.carafs` exists for development and disaster, but a crashed
  volume's normal path is replay-on-mount, invisible to the user.

---

## 6. Epic breakdown

Each epic ends green (host ctest + kernel smoke + format-check) and
commits per the standing rule. F0–F4 are pure host work — fast
iteration, no QEMU in the loop until F5.

- **F0 — format foundation.** `include/cara/carafs.h` (every struct,
  offset, magic, mask), LE codec helpers, CRC-32C, layout-assert host
  test (`static_assert` offsets + golden-bytes fixtures).
- **F1 — bdev + cache + mkfs v0.** File-backed host bdev; block
  cache; `mkfs.carafs` writes superblock + journal + AGs + empty root;
  `fsck.carafs` v0 validates structure. Host tests: mkfs → fsck
  clean; corrupt any metadata block → fsck names it.
- **F2 — cnodes, files, allocator.** Create/read/write/delete files
  via the core API; inline data; extents incl. tree spill; AG
  allocator. Host tests incl. sparse, append-fragmentation, and
  free-space accounting invariants.
- **F3 — directories.** Inline → leaf → tree promotion; lookup,
  insert, remove, iterate; case-fold; collision handling; the 10^6
  entry scale test (block-reads-counted). Hard/soft links.
- **F4 — journal.** WAL, ordered-data flush discipline, replay,
  checkpoint. The crash-injection harness (§4) at every write
  boundary of a mixed workload. After F4 the *format* is frozen:
  bump `incompat` for any later change.
- **F5 — kernel mount.** `Croi_Nvme_Flush`; kernel CarafsBdev over
  `Croi_Nvme_*`; mount at boot when a CaraFS superblock is found;
  `KERNEL_TEST(carafs_mount)` + write/reboot-persist smoke (the QEMU
  harness gets a persistent scratch image for one test stage).
- **F6 — Subgoal 3 handoff.** GPT/UUID partition discovery, root
  volume selection, `S:Startup-Sequence` analogue — scoped in the
  Logaic boot-path doc, not here.

Phase 2's success criterion then reads: Clar's drawer is a CaraFS
directory listing (replacing the Phase 1 hard-coded Bosca), edit →
write → reboot → still there.

---

## 7. Open questions

1. **Inline-data ↔ mmap-style access** (Phase 3+): if `dos.library`
   ever hands out direct SASOS pointers into the cache, inline data
   complicates it. Parked; nothing in V36 requires it.
2. **TRIM/Deallocate**: NVMe Dataset Management on extent free. Easy
   add behind a `compat` flag; needs driver support first.
3. **atime**: not maintained (changed_ns covers metadata changes).
   Revisit if some V36 tool meaningfully depends on access times.
4. **Multi-volume / multi-queue**: one volume, one NVMe namespace,
   QID 1 for Phase 2. Per-volume handler Gleasanna (Phase 3) make the
   scaling story per-process, which is the Amiga way.
5. **Defragmentation**: no online defrag; the allocator policy (§3.8)
   is the defence. A host `defrag.carafs` could ride the same core if
   fragmentation ever shows up in practice.
