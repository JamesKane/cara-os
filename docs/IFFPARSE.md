# L10 — iffparse.library

> Scope/design for Phase 3 epic **L10**, the first of the V36 "long tail"
> (`docs/PHASE3.md` §4 L10–L14): `iffparse.library` — the generic IFF
> (Interchange File Format) chunk reader/writer. It is the foundation the
> paint app needs for ILBM load/save, and the substrate the clipboard and
> (later) `datatypes` build on. Read this before cutting L10 code. Pairs
> with `docs/LOGAIC_DOS.md` (the file stream), `docs/PHASE3.md`, and
> `docs/LVO.md`.
>
> Authoritative spec: the V36+ `<libraries/iffparse.h>`,
> `iffparse_lib.fd`, and the RKM Devices/3rd-Edition "IFFParse" chapter
> (read from `amiga_docs/`, never copy). Verbatim names, struct offsets,
> error codes, chunk IDs, and LVO numbers are ABI.

---

## 1. Scope

**In scope (the IFF read + write core a paint/ILBM app uses):**

- **Handle lifecycle** — `AllocIFF` / `FreeIFF`, `InitIFFasDOS` (bind the
  handle to a dos FileHandle), `OpenIFF` (read or write mode) / `CloseIFF`.
- **The read walk** — `ParseIFF` (SCAN / STEP), `StopChunk` /
  `StopOnExit` (where to stop), `ReadChunkBytes` / `ReadChunkRecords`,
  `CurrentChunk` / `ParentChunk` (the context stack).
- **The write side** — `PushChunk` / `PopChunk`, `WriteChunkBytes` /
  `WriteChunkRecords`.
- **Property / collection gathering** — `PropChunk(s)`,
  `CollectionChunk(s)`, `FindProp` / `FindCollection` (+ the
  `StoredProperty` / `CollectionItem` structs) — the way ILBM gathers
  BMHD/CMAP before the BODY.
- **Local context items** — `StoreItemInContext` / `FindLocalItem` /
  `AllocLocalItem` / `LocalItemData` / `StoreLocalItem` (apps-gated; the
  prop/collection system is built on these).
- **ID helpers** — `GoodID` / `GoodType` / `IDtoStr`, `MAKE_ID`.
- ABI-complete headers: `<libraries/iffparse.h>` (`struct IFFHandle`,
  `ContextNode`, `StoredProperty`, `CollectionItem`, `LocalContextItem`,
  the `IFFERR_*` codes, `IFFPARSE_*` modes, `IFFF_*` flags, `ID_FORM` /
  `ID_LIST` / `ID_PROP` / `ID_CAT` / `ID_NULL`) + `<proto/iffparse.h>`.

**Out of scope (deferred / later):**

- **Custom client streams** — `InitIFF(iff, flags, hook)` with a U-mode
  stream hook (`IFFCMD_READ`/`WRITE`/`SEEK`). The hook is arbitrary U-mode
  code the S-mode impl can't call (the L1 RX-page rule), so **v0 supports
  only `InitIFFasDOS`** (the stream is a dos `BPTR` FileHandle, read/
  written kernel-side via the dos impls). `InitIFFasClip` (clipboard)
  needs `OpenClipboard` / `clipboard.device` — deferred with the
  clipboard.
- **`EntryHandler` / `ExitHandler` custom handlers** (per-context-type
  callback hooks) — same U-mode-hook constraint; the built-in
  prop/collection/stop handlers cover ILBM. Declared, stubbed.
- **`OpenClipboard` / `CloseClipboard`**, `ParseIFFsv`-style seek-back on
  malformed files beyond the basic chunk pad/`EOF`/`EOC` handling.

**Done-bar for L10:** a Gleas (or KERNEL_TEST) writes an IFF `FORM` with a
couple of chunks to a dos file (`PushChunk`/`WriteChunkBytes`/`PopChunk`),
closes it, reopens it, `ParseIFF`-walks to a `StopChunk`'d chunk, reads
its bytes back with `ReadChunkBytes`, and the round-trip matches — plus a
`PropChunk`/`FindProp` gather — with ABI-complete declaration + stub
coverage for the rest.

---

## 2. The key decisions

### 2.1 A new `src/croi/iffparse`, syscall, over a dos FileHandle

iffparse is pure parsing logic with **no per-task UI state**, but it does
I/O against a stream. v0's stream is always a **dos FileHandle** (`BPTR`),
so the natural place for the impl is kernel-side: each LVO is `syscall`,
and `ReadChunkBytes`/`WriteChunkBytes` call the dos impls
(`Croi_Dos_Read_Impl` / `Croi_Dos_Write_Impl` / `Croi_Dos_Seek_Impl`)
directly. The `IFFHandle` + the parse context stack live in the **SASOS
shared heap** (`Croi_AllocShared`), valid in U-mode.

Mirror the gadtools/asl library layout:

- **New riscv64-only dir `src/croi/iffparse`** = the kernel-side
  `iffparse.library`: `Croi_Iff_*_Impl` bodies, the generated
  `iffparse_vec.c`, `trampolines.S` (`.lib_text.iffparse`, KEEP'd), the
  reserved-hook TU. Whole-archived into croi.
- `tools/lvo-gen/iffparse.conf` (`##library iffparse.library`,
  `##base IFFParseBase`, `##owner croi/iffparse`), added to
  `lvo-coverage`.
- Construction block in `entry.c`.

No Irish brand — named after the library (`iffparse` / `Croi_Iff_*`), as
gadtools/asl are. All `syscall`; the few wide LVOs are still ≤7 args.

### 2.2 IFFHandle + the kernel-private parse state

The public `struct IFFHandle` is small (`iff_Stream`, `iff_Flags`,
`iff_Depth`). CaraOS hangs the parse machinery off a kernel-private
`CaraIff` whose public `IFFHandle` is at offset 0 (the AllocAslRequest
pattern), so `AllocIFF` returns `&ci->pub` and the impls recover `CaraIff`
by cast:

```c
struct CaraIff {
    struct IFFHandle pub;     // offset 0 — iff_Stream = dos BPTR, iff_Flags
    BPTR stream;              // the dos FileHandle (InitIFFasDOS)
    int depth;               // context-stack depth
    struct CaraIffCtx *top;  // context stack (FORM/chunk ContextNodes)
    // stop/prop/collection registries (small fixed arrays in v0)
};
```

Each `ParseIFF` push allocates a `ContextNode` (cn_Type/cn_ID/cn_Size/
cn_Scan) onto the stack; `CurrentChunk`/`ParentChunk` return them.
`cn_Scan` tracks how many of the chunk's bytes have been consumed so
`ReadChunkBytes` clamps to the chunk and `ParseIFF` skips the remainder +
the pad byte.

### 2.3 The on-disk format (what the impls actually do)

IFF is big-endian 4-byte IDs + big-endian `ULONG` sizes, chunks padded to
even length. `ParseIFF`:

- At the top, read the `FORM`/`LIST`/`CAT` header (ID + size + the
  form-type ID), push a context node.
- `IFFPARSE_STEP`: read the next chunk header (ID + size), push a context
  node, return (the client reads the body). On re-entry, skip any
  unread body + pad, pop, read the next.
- `IFFPARSE_SCAN`: STEP until a registered `StopChunk` chunk is entered,
  then return 0 (the client reads it); `IFFERR_EOF` at end of the FORM.
- Gather: chunks registered via `PropChunk` are slurped into a
  `StoredProperty` (FindProp), `CollectionChunk` into a `CollectionItem`
  list (FindCollection), as they're stepped over.

Writing is the mirror: `PushChunk(type,id,size)` writes the header (size
`IFFSIZE_UNKNOWN` → backpatched at `PopChunk` via `Seek`),
`WriteChunkBytes` appends, `PopChunk` pads + backpatches the size.

### 2.4 Errors

Verbatim `IFFERR_*` (0 = success; negatives): `IFFERR_EOF (-1)`,
`IFFERR_EOC (-2)`, `IFFERR_NOSCOPE (-3)`, `IFFERR_NOMEM (-4)`,
`IFFERR_READ (-5)`, `IFFERR_WRITE (-6)`, `IFFERR_SEEK (-7)`,
`IFFERR_MANGLED (-8)`, `IFFERR_SYNTAX (-9)`, `IFFERR_NOTIFF (-10)`,
`IFFERR_NOHOOK (-11)`, … (confirm the tail against the FD).

---

## 3. LVO offsets (canonical — **verify at L10.1**)

From `iffparse_lib.fd` (reserved 0..3 = -6..-24; confirm each against
`amiga_docs/` — offsets are ABI + feed Phase 9). All implemented rows
`syscall`. (The exact order past the first dozen is fuzzy from memory;
the L10.1 task is to transcribe the FD precisely and `##pad_run` the
unimplemented slots.)

| LVO | offset | slice |
|-----|--------|-------|
| `AllocIFF`        | -30  | L10.1 |
| `OpenIFF`         | -36  | L10.1 |
| `CloseIFF`        | -42  | L10.1 |
| `ParseIFF`        | -48  | L10.2 |
| `ReadChunkBytes`  | -54  | L10.2 |
| `WriteChunkBytes` | -60  | L10.3 |
| `ReadChunkRecords`| -66  | L10.2 |
| `WriteChunkRecords`| -72 | L10.3 |
| `PushChunk`       | -78  | L10.3 |
| `PopChunk`        | -84  | L10.3 |
| `EntryHandler`    | -90  | stub |
| `ExitHandler`     | -96  | stub |
| `PropChunk(s)`    | -102/-108 | L10.4 |
| `StopChunk(s)`    | -114/-120 | L10.2 |
| `CollectionChunk(s)`| -126/-132 | L10.4 |
| `StopOnExit`      | -138 | L10.2 |
| `FindProp`        | -144 | L10.4 |
| `FindCollection`  | -150 | L10.4 |
| `CurrentChunk`    | -156 | L10.2 |
| `ParentChunk`     | -162 | L10.2 |
| `AllocLocalItem`/`LocalItemData`/… | -168.. | L10.4 |
| `FreeIFF`         | -…   | L10.1 |
| `InitIFFasDOS`    | -…   | L10.1 |
| `InitIFFasClip`   | -…   | stub |
| `InitIFF`         | -…   | stub (custom hooks deferred) |
| `GoodID`/`GoodType`/`IDtoStr` | -… | L10.1 |

---

## 4. Slice plan

- **L10.1 — library + handle lifecycle.** `iffparse.conf` +
  `IFFParseBase` + boot; `<libraries/iffparse.h>`. `AllocIFF`/`FreeIFF`,
  `InitIFFasDOS`, `OpenIFF`/`CloseIFF` (read: validate the FORM header;
  write: prime the stream), `GoodID`/`GoodType`/`IDtoStr`. **Test:**
  alloc + init-as-DOS over an opened dos file + open/close round-trip.
- **L10.2 — the read walk.** `ParseIFF` (STEP + SCAN), `StopChunk`/
  `StopOnExit`, `ReadChunkBytes`/`ReadChunkRecords`, `CurrentChunk`/
  `ParentChunk`. **Test:** parse a hand-written FORM, stop on a chunk,
  read its bytes.
- **L10.3 — the write side.** `PushChunk`/`PopChunk` (size backpatch via
  Seek), `WriteChunkBytes`/`WriteChunkRecords`. **Test:** write a FORM
  with two chunks → reopen → L10.2 reads them back (the round-trip
  done-bar).
- **L10.4 — props / collections / context items.** `PropChunk(s)`/
  `FindProp` (+ `StoredProperty`), `CollectionChunk(s)`/`FindCollection`
  (+ `CollectionItem`), `StoreItemInContext`/`FindLocalItem`/
  `AllocLocalItem`/`LocalItemData`. **Test:** gather a prop chunk while
  scanning to the BODY, `FindProp` it back.

---

## 5. Testing

iffparse is `syscall` over a dos FileHandle, so the impls are callable
from the S-mode runner and the stream is a real CaraFS file (mounted at
runner time, as the L3/Clar tests use). A `KERNEL_TEST` opens a temp dos
file, writes an IFF FORM (L10.3), closes, reopens, parses it back
(L10.2), and asserts the chunk bytes match — the canonical IFF round-trip.
A Gleas can additionally drive the full `OpenLibrary("iffparse.library")`
path. (Custom in-memory streams would simplify the test but are the
deferred custom-hook path; v0 uses a dos file.)

---

## 6. Tracked gaps / deferrals

- Custom client streams (`InitIFF` + stream hooks) and `InitIFFasClip` /
  `OpenClipboard` (needs clipboard.device).
- `EntryHandler`/`ExitHandler` custom context handlers.
- Deep malformed-file recovery; `ParseIFF` seek-back edge cases.
- `datatypes.library` (which layers on iffparse) — a later long-tail epic.
- The downstream **ILBM** load/save itself lives in the paint app (epic
  A) / a helper, not in iffparse.
