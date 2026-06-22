# L11 — icon.library (scope)

Cleanroom V36+ `icon.library` for CaraOS Phase 3. The library that turns a
Workbench `.info` icon into a `struct DiskObject` — except CaraOS does not
keep `.info` sidecar files. Icon metadata lives **on the object**, in the
`cara.icon` extended attribute (CARAFS §3.10). So L11 is two things stacked:
a CaraFS **xattr storage layer** (reserved in the format, never coded) and
the `icon.library` that (de)serialises a `DiskObject` over it.

This is the first slice of the L11–14 "long tail" (icon, diskfont,
commodities, expansion). It is **apps-driven** (`docs/PHASE3.md §4`): the
file-manager is the consumer — it needs an object's type, its tool types,
and its Workbench position. That set, plus the round-trip to write them
back, is what gets a body; the rest is ABI-complete and stubbed.

Read alongside: `docs/CARAFS.md §3.10` (the xattr decision), `docs/LVO.md`
(flavours, the `local`/`syscall` split), `docs/IFFPARSE.md` (the most
recent scope doc — same shape).

---

## 1. Scope

**In scope (gets a working body):**

- A CaraFS **inline xattr layer**: get / set / remove a named attribute on
  a cnode, reusing the existing `carafs_item_*` TLV helpers and the
  reserved `CARAFS_ITEM_XATTR` item kind. Inline only.
- `icon.library` base + dispatch (a base-ful `syscall` library, the
  iffparse/asl recipe).
- Read path: `GetDiskObject` / `GetDiskObjectNew` / `GetDefDiskObject` /
  `FreeDiskObject` — parse `cara.icon` into a heap `DiskObject`.
- Write path: `PutDiskObject` / `PutDefDiskObject` / `DeleteDiskObject` —
  serialise a `DiskObject` back to the xattr (or remove it).
- Tool-type helpers: `FindToolType` / `MatchToolValue` (pure string,
  `local` flavour) and `BumpRevision`.

**Out of scope (declared ABI-complete, defined stub):**

- **Icon imagery.** `do_Gadget.GadgetRender` / `SelectRender` are `nullptr`
  in v0; the file-manager draws a default chunky glyph keyed on `do_Type`.
  No planar `Image` blobs, no colour-icon (`IFF FORM ICON`) chunks.
- **The legacy layer:** `GetWBObject` / `PutWBObject` / `GetIcon` /
  `PutIcon` (the pre-2.0 freelist-exposed API).
- **Freelist management:** `AddFreeList` / `FreeFreeList` /
  `AllocWBObject`. Our `DiskObject` is one shared-heap allocation freed by
  `FreeDiskObject`; there is no caller-visible `FreeList`.
- **The `XATTR_OVERFLOW` chain.** A serialised default `DiskObject` is tens
  of bytes; it always fits the cnode inline item area. Values that would
  spill (large `user.*` attrs) are a later slice.
- **AppIcons / AppWindows** — a `workbench.library` concern, not icon.

**Done-bar:** the file-manager can ask "what is this object, where does it
sit, what are its tool types?" and write a changed position/tool type back,
persisting across reboot — proven by a Gleas round-trip
(`PutDiskObject` → reboot → `GetDiskObject` returns the same fields).

---

## 2. The key decisions

### 2.1 Metadata is an xattr, not a `.info` file

Classic Workbench stored icon data in a sibling `foo.info` file. CaraOS
stores it in the `cara.icon` xattr **of `foo` itself** (CARAFS §3.10):

> CaraFS stores it *on the object*: the `cara.icon` xattr holds what
> `icon.library` will parse — icon imagery reference, drawer window
> geometry, tool types. Namespaced names (`cara.*` reserved, `user.*`
> free-form); value ≤ 64 KiB (inline first, then `XATTR_OVERFLOW` chain).

So `GetDiskObject("foo")` resolves the object `foo`, reads its `cara.icon`
xattr, and deserialises. There is **no name mangling to `foo.info`** and no
second directory entry to orphan. Whether `dos.library` later synthesises a
virtual `foo.info` for a V36 program that `Open`s it directly is a separate
Logaic decision (deferred); icon.library never needs it.

### 2.2 The CaraFS xattr layer is new code over a frozen format

The on-disk format already **reserves** everything xattrs need — no format
change, F4 freeze holds:

- `CARAFS_ITEM_XATTR = 3` and `CARAFS_ITEM_XATTR_OVERFLOW = 7` (item kinds
  in the cnode TLV area), `CARAFS_MAGIC_XATTR` and `struct CarafsXattrBlock`
  (the overflow block) — all defined in `include/cara/carafs.h`, **unused**.
- `carafs_item_find()` / `carafs_item_resize()` (`src/logaic/carafs/cnode.c`)
  already read/write arbitrary item kinds in the cnode — they back NAME,
  COMMENT, INLINE_DATA, INLINE_DIRENTS, SYMLINK_TARGET today.

What is missing is the **layer**: there is no `Carafs_Xattr_Get/Set` and no
path to reach it. L11.1 builds it as a thin wrapper over the existing item
helpers, storing **one `CARAFS_ITEM_XATTR` item** whose payload is a packed
record list (multiple named attrs share the item):

```
item payload  ::=  record*
record        ::=  u8 name_len | u8 flags | u16 val_len | name[name_len] | val[val_len]
```

`Get(name)` scans for a matching record; `Set(name,val)` rewrites the item
with the record replaced/appended (then `carafs_item_resize` + flush the
cnode); `Remove(name)` drops it. **Inline only** — if a `Set` won't fit the
cnode item budget it returns "too big" (the `XATTR_OVERFLOW` chain is
deferred; `cara.icon` never hits it). Defining the record layout *inside*
the already-reserved item kind completes the format, it does not change it —
no existing volume carries an xattr item.

### 2.3 `syscall` flavour; path → cnode via a dos kernel bridge

icon.library is base-ful and `syscall`-flavour, exactly like iffparse and
asl: each LVO is a trampoline (`Cara_Trampoline_Icon_*`) that `ecall`s into
Croi, and `src/croi/syscall/syscall.c` routes `SYS_Icon_*` to
`Croi_Icon_*_Impl`. The impls run S-mode and (de)serialise kernel-side.

The xattr lives on a cnode, so an impl must resolve a path → cnode. The
kernel already resolves paths for the iffparse file bridge
(`Croi_Dos_Open_Impl`, `Croi_Dos_Lock_Impl` in `include/cara/dos_lib.h`).
L11 adds the cnode-flavoured bridge in the carafs/dos kernel layer —
`Croi_Fs_Xattr_Get/Set(BPTR lock, name, buf, len)` — that turns a dos Lock
into a cnode, runs the §2.2 accessor, and flushes. icon.library does
`Lock(name)` → xattr op → `UnLock`. (Exact module placement — carafs vs a
`croi` bridge — is an L11.1 detail; the seam is: *icon never touches
CaraFS structs; it goes through one `Croi_Fs_Xattr_*` helper*.)

### 2.4 The `cara.icon` blob is a CaraOS-native serialisation

Because we own `cara.*`, the xattr value is **not** the classic binary
`.info` layout (Hunk-ish `DiskObject` + planar `Image` + counted strings).
It is a compact, versioned CaraOS blob carrying only what we model:

```
CaraIconBlob ::= u16 magic (0xCA10) | u16 version (1)
               | u8  type        (WBDISK..WBAPPICON)
               | u8  flags
               | i32 currentX | i32 currentY   (NO_ICON_POSITION = 0x80000000)
               | u32 stackSize
               | cstr defaultTool                (counted: u16 len + bytes)
               | cstr toolWindow
               | u16 n_toolTypes | cstr toolType[n_toolTypes]
               | (if type==WBDRAWER) DrawerGeom   (i16 x,y,w,h + i32 ddCurrentX,ddCurrentY)
```

`GetDiskObject` allocates one shared-heap block holding the `DiskObject`,
its `do_ToolTypes` `char **` vector + the string bytes, and (for a drawer)
a `DrawerData` — so `FreeDiskObject` is a single free. Imagery fields are
`nullptr`. `PutDiskObject` walks the live `DiskObject` and re-emits the
blob. Versioned so a future imagery-carrying v2 is additive.

### 2.5 `FindToolType` / `MatchToolValue` are `local` flavour

These are pure string operations on the caller's `do_ToolTypes` vector — no
kernel state, no IO. They run in-process in the library RX page, like the
exec list ops (`docs/HANDOFF` L1 pattern): subject to the RX-page rules
(self-contained, force-inlined helpers, no out-of-section calls). This
keeps the file-manager's hot "does this icon have tool type X?" query off
the syscall path. `BumpRevision` (make a "copy_N_of_name" string) is also
`local`.

---

## 3. LVO surface

Bias 30; reserved slots 0–3 (`Open`/`Close`/`Expunge`/`ExtFunc`) are
`local` hooks. **Offsets below are the canonical V36+ `icon_lib.fd` values;
they are locked against `amiga_docs/` when `tools/lvo-gen/icon.conf` is
written in L11.1** (cross-check, never copy — `CLAUDE.md`). `##pad_run`
keeps declaration order aligned to the canonical numbers (ordinal =
`|lvo|/6 − 1`).

| LVO | flavour | slice | notes |
|-----|---------|-------|-------|
| `GetWBObject` / `PutWBObject` / `GetIcon` / `PutIcon` | — | **stub** | legacy pre-2.0 freelist API |
| `FreeFreeList` / `AddFreeList` | — | **stub** | no caller-visible FreeList |
| `GetDiskObject` | syscall | L11.1 | read `cara.icon` → DiskObject |
| `FreeDiskObject` | syscall¹ | L11.1 | one shared-heap free |
| `GetDiskObjectNew` | syscall | L11.2 | GetDiskObject, default if absent |
| `GetDefDiskObject` | syscall | L11.2 | default icon for a `WB*` type |
| `PutDiskObject` | syscall | L11.2 | serialise → `cara.icon` |
| `PutDefDiskObject` | syscall | L11.2 | write the type default |
| `DeleteDiskObject` | syscall | L11.2 | remove the `cara.icon` xattr |
| `FindToolType` | local | L11.3 | string scan of `do_ToolTypes` |
| `MatchToolValue` | local | L11.3 | `a|b|c` value match |
| `BumpRevision` | local | L11.3 | "copy_N_of_" name builder |

¹ `FreeDiskObject` is `syscall` because the DiskObject lives in the SASOS
shared heap that the impl allocated; freeing it is a kernel-heap op. (If a
`local` free over a shared allocator lands first, it can move.)

Everything not listed with a slice is declared at its canonical LVO and
emitted as a defined stub (returns `0` / `nullptr` / `DOSFALSE`), so a V36
program links.

---

## 4. Slice plan

### L11.1 — xattr substrate + library foundation + read

- CaraFS inline xattr layer (§2.2): `Carafs_Xattr_Get/Set/Remove` over
  `carafs_item_*` + the `CARAFS_ITEM_XATTR` record format; the
  `Croi_Fs_Xattr_*(BPTR lock, …)` kernel bridge (§2.3).
- `include/workbench/workbench.h` — verbatim `struct DiskObject`,
  `struct DrawerData`, `WBDISK..WBAPPICON`, `WB_DISKMAGIC`/`WB_DISKVERSION`,
  `NO_ICON_POSITION`. `tools/lvo-gen/icon.conf` (full surface, offsets
  locked here) → `proto/icon.h` / `icon/lvo.h` / `icon_vec.c`. The
  `src/croi/icon` library (base, hooks, trampolines, MakeLibrary in
  `entry.c`, `KEEP(.lib_text.icon)` in `croi.lds`, coverage wiring).
- `GetDiskObject` (§2.4 deserialise) + `FreeDiskObject`.
- **Test (KERNEL_TEST):** seed a cnode's `cara.icon` via `Carafs_Xattr_Set`
  with a hand-built blob; `Croi_Icon_GetDiskObject_Impl` returns a
  DiskObject with the expected `do_Type`, `do_CurrentX/Y`, and tool types;
  `FreeDiskObject` balances. (No path resolution needed — drive the
  accessor on a cnode directly, like L10.1 used a fake stream.)

### L11.2 — write side + defaults

- `PutDiskObject` (serialise) / `DeleteDiskObject` (remove xattr) /
  `PutDefDiskObject`; `GetDefDiskObject` (synthesise a default DiskObject
  per `WB*` type) and `GetDiskObjectNew` (Get, else GetDef).
- **Test (Gleas round-trip):** `userexec`/`userintuition`-style — create a
  file, `PutDiskObject` a drawer with a position + two tool types,
  `GetDiskObject` it back, assert the fields match. The reboot leg is
  covered by the existing two-boot smoke (write before reboot, read after).
  Path-resolving calls need a Process, so this is a Gleas test, not a
  KERNEL_TEST (the L10 constraint).

### L11.3 — tool types + revision (local flavour)

- `FindToolType`, `MatchToolValue`, `BumpRevision` in a `.lib_text.icon`
  RX-page TU (RX-page rules apply).
- **Test:** host-style logic over a static `char *[]` tool-type vector
  exercised from the Gleas — `FindToolType(tt,"CX_PRIORITY")` returns the
  value, `MatchToolValue(v,"on")` matches `on|off`, `BumpRevision` builds
  `copy_2_of_foo`.

---

## 5. Testing

- **Substrate** (L11.1): pure cnode-level xattr get/set/remove is a
  `KERNEL_TEST` (and a host unit test if the record codec is factored
  shared, like the FDT/HID decoders).
- **icon read** (L11.1): KERNEL_TEST driving the accessor on a seeded cnode.
- **icon write + defaults** (L11.2): Gleas round-trip + the two-boot smoke
  for persistence.
- **tool types** (L11.3): Gleas, pure-string assertions.

Each slice ends on the standing gate: host `ctest` green, in-kernel runner
`0 failed`, format-check clean, two-boot QEMU smoke `ok`; commit; regenerate
`docs/LVO_COVERAGE.md`; handoff/memory follow-up.

---

## 6. Tracked gaps / deferrals

- **Imagery** — no `Image`/`GadgetRender`, no colour-icon `IFF FORM ICON`.
  The file-manager renders a default glyph per `do_Type`. Forced forward
  only when paint/Workbench needs real icon bitmaps; a v2 `CaraIconBlob`
  carries an imagery reference additively.
- **`XATTR_OVERFLOW` chain** — inline-only in v0; large `user.*` xattrs and
  the `CarafsXattrBlock` chain wait until something needs > the cnode item
  budget.
- **Legacy API** (`GetWBObject`/`GetIcon`/…) and **freelists**
  (`AddFreeList`/`FreeFreeList`/`AllocWBObject`) — stubbed; no modern caller.
- **Virtual `foo.info`** via dos `Open` — a Logaic decision, not needed by
  any icon-aware program.
- **dos metadata setters** — `SetComment`/`SetProtection`/`SetFileDate` are
  the *other* unimplemented CaraFS attribute path (`LOGAIC_DOS.md`); they
  share the cnode-item plumbing L11.1 builds, so wiring them is a cheap
  follow-on but stays out of L11.
- **AppIcon / AppWindow / AppMenu** — `workbench.library`, a later epic.
