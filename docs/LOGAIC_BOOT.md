# Logaic boot path — Phase 2 Subgoal 3

> The Phase 2 Subgoal 3 design document (`docs/ROADMAP.md`). Subgoals 1
> and 2 produced the NVMe driver (`docs/PHASE2_NVME.md`) and the CaraFS
> filesystem (`docs/CARAFS.md`, epics F0–F5: the format is frozen and a
> volume mounts on NVMe at boot and survives reboot). Subgoal 3 turns a
> raw NVMe namespace into a *partitioned, discoverable, user-visible*
> root volume — the last piece of Phase 2's success criterion:
>
> > Croi mounts a CaraFS volume on an NVMe SSD; Clar boots from that
> > volume, edits a file in a drawer, and the change persists across
> > reboot.
>
> Cleanroom rule applies (PRINCIPLES §2): GPT is read from the UEFI
> specification and reimplemented; no third-party partition/FS code.

---

## 1. Scope and the one load-bearing decision

What stands between F5 and the criterion:

1. **Partitioning.** A real M.2 SSD is GPT-partitioned; the FS lives in a
   *partition*, not at LBA 0 of the namespace. Boot must discover the
   GPT, find the CaraFS partition, and mount *that* (§3).
2. **Root selection.** When several CaraFS partitions exist, pick the
   root by volume UUID (the superblock UUID, §3.3 of CARAFS.md). One
   volume for Phase 2; the mechanism generalises.
3. **User-visible files.** Clar (a U-mode Gleas) must list a real
   directory and read/write a file. The mount is S-mode kernel state, so
   this needs a privilege-crossing path (§2).
4. **Startup.** An `S:Startup-Sequence` analogue executed at boot (§5).

### The decision: thin `Croi_Fs_*` syscalls now, `dos.library` in Phase 3

Clar needs filesystem access across the U/S boundary. There are two ways:

- **Thin kernel syscalls** — a handful of brand-namespace `Croi_Fs_*`
  ecalls that wrap the kernel mount's `Carafs_*` ops (open/readdir/
  read/write/create). Kernel-internal, so they may use the `Croi_`
  prefix (PRINCIPLES §3.1). Small, bounded, exactly enough for the
  criterion.
- **A first slice of `dos.library` / Logaic** — locks, `DosPacket`s, a
  handler Gleas the mount lives inside, the canonical AmigaDOS process
  model.

We take the **syscall** path. `dos.library` is **Phase 3** work
(ROADMAP Phase 3 Subgoal 2, implemented by Logaic); building it now
would violate phase discipline (PRINCIPLES §6 — don't build a later
phase before the current one's criterion is met). The `Croi_Fs_*`
syscalls are a deliberate stopgap: Phase 3's `dos.library` will be
re-platformed onto them (or onto a handler Gleas that owns the mount),
and the brand/API namespace split means no user-visible AmigaDOS name is
spent here. The eventual home of the CaraFS core is an AmigaDOS handler
Gleas (CARAFS.md §4, ARCHITECTURE §6); these syscalls are the Phase 2
stand-in, nothing more.

**Out of scope for Subgoal 3:** `dos.library`, locks, `DosPacket`s,
BCPL strings, assigns/volume-relative path syntax beyond the bare
minimum, multi-volume mounting, FAT (the SD card stays the fallback boot
route, ROADMAP Phase 2 out-of-scope).

---

## 2. On-device layout

```
NVMe namespace LBA space:
  LBA 0            protective MBR (one 0xEE partition spanning the disk)
  LBA 1            primary GPT header
  LBA 2 ..         partition entry array (128 × 128 B = 16 KiB)
  ...              [ CaraFS partition: block 0 = superblock, ... ]
  last - 32 ..     backup partition entry array
  last LBA         backup GPT header
```

A CaraFS volume occupies one GPT partition; CaraFS's own internal layout
(superblock at the partition's block 0, journal, AGs, backup superblock)
is unchanged from CARAFS.md §3.2 — it just starts at the partition's
first LBA rather than namespace LBA 0. The kernel `CarafsBdev` adds the
partition base offset (§3); the CaraFS core is oblivious to partitioning.

**CaraFS partition type GUID** (minted for this project, stable):

```
CARAFS_GPT_TYPE_GUID = CA1A0F50-0000-4361-7261-465321000000
                       (mixed-endian GPT GUID encoding; see gpt.h)
```

The partition's *unique* GUID is independent of the CaraFS superblock
UUID; root selection keys on the **superblock UUID** (an FS identity),
not the GPT partition GUID, so reformatting a partition keeps its slot.

---

## 3. GPT discovery and mount (epic G1)

Cleanroom GPT, read from the UEFI spec. `cara_gpt` is a portable,
allocation-free library behind a `GptDev` block seam (lba_size, n_lbas,
read/write) — the FDT-parser / CarafsBdev pattern, host-tested with a
memory device, kernel-bound to NVMe.

- **Parse + validate.** GPT header at LBA 1: signature `"EFI PART"`,
  header CRC-32 (field zeroed during compute), `my_lba`, `alternate_lba`,
  the entry-array location/size/count, and the entry-array CRC-32.
  CRC-32 here is **IEEE 802.3** (reflected, poly `0xEDB88320`) — *not*
  CaraFS's CRC-32C. Both header CRCs must check out.
- **Find.** Walk the entries; return the first whose type GUID is
  `CARAFS_GPT_TYPE_GUID` (G1) / whose volume matches a target UUID (G2).
- **Format.** When the namespace has no valid GPT, lay one down:
  protective MBR + primary & backup GPT headers + entry arrays + one
  CaraFS partition spanning the usable LBAs. Symmetric with F5's
  mkfs-on-empty — the kernel partitions a blank disk, then mkfs's the
  partition. Host-tested by a format→parse round trip.

**Boot flow** (`Croi_Carafs_BringUp`, `src/croi/carafs_bind.c`): NVMe up
→ read the GPT → if none, format it → find the CaraFS partition → set
the `CarafsBdev` base LBA + block count to that partition → mount (mkfs
the partition if it has no superblock) → `g_carafs_mounted`. The
two-boot smoke proves the marker file persists, now *inside a
partition*.

---

## 4. Filesystem syscalls + Clar's drawer (epic G3 — the criterion)

A minimal, kernel-internal syscall surface over the boot mount:

| syscall | wraps | purpose |
|---|---|---|
| `Croi_Fs_OpenDir` / `ReadDir` | `Carafs_DirNext` | list a drawer |
| `Croi_Fs_Lookup` / `Stat` | `Carafs_DirLookup` / `Carafs_CnodeStat` | resolve a name |
| `Croi_Fs_Read` / `Write` | `Carafs_FileRead` / `Carafs_FileWrite` | file contents |
| `Croi_Fs_Create` / `Delete` | `Carafs_DirCreate` / `Carafs_DirRemove` | new / remove |

Clar (U-mode) lists the root directory into its drawer instead of the
hard-coded Bosca, opens a file into the text Inntin, and on save writes
it back. `edit → write → reboot → still there` is the criterion; the
smoke harness already boots twice against one image, so a Clar-written
file checked on the second boot closes it.

Concurrency: one mounter (CARAFS.md §4). Phase 2 is single-Gleas-at-a-
time access to the FS through these syscalls, serialised in the kernel;
Phase 3's handler Gleas makes it properly per-process.

---

## 5. Startup-Sequence analogue (epic G4)

A boot script read from the root volume (`S/Startup-Sequence`) and
interpreted by a minimal command runner — the AmigaDOS boot idiom. For
Phase 2 this can be as small as "if present, run the listed Gleasanna;
else launch Clar". Kept deliberately tiny; the full AmigaDOS shell is
Phase 3 (`dos.library` + the `Ed`/`List`/… tools).

---

## 6. Epic breakdown

Each epic ends green (host ctest + kernel smoke + format-check) and
commits per the standing rule. G1 is mostly host work (the parser);
G3 is where the U/S boundary and Clar UI work concentrate.

- **G1 — GPT discovery.** `cara_gpt` (parse/validate/find/format, CRC-32,
  the CaraFS type GUID) with host tests; kernel binding reads/lays the
  GPT and offsets the mount to the CaraFS partition. Two-boot smoke
  green within a partition. **(this epic)**
- **G2 — root selection by UUID.** Multiple-partition handling; pick the
  root by superblock UUID; a host tool (`mkgpt`/`mkfs --gpt`) so disks
  can be authored off-target.
- **G3 — FS syscalls + Clar drawer.** The `Croi_Fs_*` surface; Clar
  lists a real directory, edits and saves a file; the persist smoke
  checks a Clar-written file across reboot. **Carries the criterion.**
- **G4 — Startup-Sequence.** The boot-script runner; wires Clar launch
  through it. Closes Phase 2.

---

## 7. Open questions

1. **Backup-GPT repair.** Should a torn primary GPT auto-recover from the
   backup at mount (like CaraFS replay)? Parser reads both; auto-repair
   is a G2+ nicety, not required for the criterion.
2. **Partition resize / multiple CaraFS partitions.** One volume for
   Phase 2; the GPT writer lays exactly one. Multi-partition authoring
   is the host tool's job (G2).
3. **Alignment.** Partitions are 1 MiB-aligned by convention (good for
   4 Kn / flash erase blocks); we follow it even though QEMU's nvme is
   512e. Documented in `gpt.c`.
