# L9 — asl.library

> Scope/design for Phase 3 epic **L9**: `asl.library` — the V36+
> "Application Support Library" that provides the system **file**, **font**
> and **screen-mode** requesters. Read this before cutting L9 code. Pairs
> with `docs/LEARGAS_GADTOOLS.md` (L8 — the gadget kinds the requesters
> are built from), `docs/LEARGAS_INTUITION.md` (L5 — the modal requester
> core this reuses), `docs/LOGAIC_DOS.md` (directory listing), and
> `docs/LVO.md`.
>
> Authoritative spec: the V36+ `<libraries/asl.h>`, `asl_lib.fd`, and the
> RKM Libraries "ASL" chapter (read from `amiga_docs/`, never copy).
> Verbatim names, struct field offsets, request-type codes, tag values,
> and LVO numbers are ABI.

---

## 1. Scope

**In scope (the asl surface an app uses to ask for a file/font):**

- **Requester lifecycle** — `AllocAslRequest(reqType, tags)` /
  `FreeAslRequest(req)`, and the modal run `AslRequest(req, tags)`.
- **The file requester** (`ASL_FileRequest`) — returns the chosen drawer
  + filename in a `struct FileRequester` (`rf_Dir` / `rf_File`).
- **The font requester** (`ASL_FontRequest`) — returns the chosen
  `struct TextAttr` in a `struct FontRequester` (`fo_Attr`).
- **The screen-mode requester** (`ASL_ScreenModeRequest`, V38) — minimal:
  the single CaraOS display mode.
- **Legacy V36 entry points** — `AllocFileRequest` / `FreeFileRequest` /
  `RequestFile` (thin wrappers over the AslRequest path).
- ABI-complete headers: `<libraries/asl.h>` (`struct FileRequester`,
  `FontRequester`, `ScreenModeRequester`, the `ASL_*` request-type codes,
  the `ASLFR_*` / `ASLFO_*` / `ASLSM_*` tags), `<proto/asl.h>` (generated).

**Out of scope (deferred / later):**

- **The browsing file list.** A faithful file requester shows a
  **scrolling list of the directory's entries** (a gadtools LISTVIEW) the
  user double-clicks to navigate/pick. LISTVIEW is the one gadtools kind
  L8 deferred (it needs a scroller + text rows + selection). So **v0 ships
  a string-entry file requester**: a drawer field + a file field + OK /
  Cancel — the user types (or the caller pre-fills) the path; no live
  directory browse. The dos `Examine`/`ExNext` listing + LISTVIEW browse
  is the tracked enhancement (§6).
- **Real font enumeration / screen-mode database.** CaraOS has one font
  (the Dath 8×8 face) and one display (no `diskfont.library`, no mode
  database — that's Phase 4 RTG). The font requester offers the single
  face; the screen-mode requester returns the single mode.
- **Multi-select file requests** (`ASLFR_DoMultiSelect` / `rf_ArgList`),
  **pattern matching** (`ASLFR_DoPatterns`), **save-mode confirmation**,
  custom hooks (`ASLFR_FilterFunc`/`ASLFR_HookFunc`), drag-resize. v0
  honours the common positioning/title/initial-path tags; the rest are
  accepted and ignored.

**Done-bar for L9:** a V36 Gleas `AllocAslRequest(ASL_FileRequest, …)`,
`AslRequest`s it (with an injected OK), reads back `rf_Dir`/`rf_File`, and
`FreeAslRequest`s — plus the same round-trip for `ASL_FontRequest`
(`fo_Attr`), the legacy `RequestFile` path, and ABI-complete declaration
+ stub coverage for the deferred surface.

---

## 2. The key decisions

### 2.1 asl over Leargas + gadtools + the L5 modal core

A requester is a **modal window of gadtools gadgets**. CaraOS already has
every piece:

- **The L5 modal requester core** — `Croi_Requester_Build` opens a
  centred modal window with bool gadgets + an IDCMP port, and
  `Croi_Requester_Wait` runs the nested IDCMP loop (blocking on the
  requester port via `Croi_Wait`, with the pre-post-GADGETUP seam that
  makes it unit-testable). asl reuses this shape for OK/Cancel.
- **gadtools kinds (L8)** — `STRING_KIND` for the drawer/file fields,
  `BUTTON_KIND` for OK/Cancel, `CYCLE_KIND` for the font choice. (The file
  browse list would be `LISTVIEW_KIND` — deferred.)

So asl is a thin **composition layer**: build a window, populate it with
the right gadgets for the request type, run the modal loop, read the
gadget values back into the requester struct. No new substrate.

### 2.2 A new `src/croi/asl` library, all `syscall`

`asl.library` is base-ful (`AslBase`), constructed at boot via
`Croi_MakeLibrary` like gadtools/intuition. Mirror the gadtools dir
layout (kernel-side bridge):

- **New riscv64-only dir `src/croi/asl`** = the kernel-side
  `asl.library`: `Croi_Asl_*_Impl` bodies (composing `Leargas_*`,
  the gadtools `Croi_GT_*_Impl`, and the L5 requester core), the generated
  `asl_vec.c`, `trampolines.S` (`.lib_text.asl`, KEEP'd in `croi.lds`),
  the reserved-hook TU. Whole-archived into croi.
- `tools/lvo-gen/asl.conf` (`##library asl.library`, `##base AslBase`,
  `##owner croi/asl`), added to `lvo-coverage`.
- Construction block in `entry.c`.

No Irish brand — named after the library (`asl` / `Croi_Asl_*`), as
gadtools/intuition_lib are. **All `syscall`** (every LVO ≤7 args); the
kernel allocates the requester structs in the **SASOS shared heap** and
runs the modal loop kernel-side.

### 2.3 The requester structs + `AllocAslRequest`

`AllocAslRequest(reqType, tags)` allocates the right struct (shared heap),
stashes the config tags' values, and returns it as an opaque `APTR`. v0
struct shapes (`<libraries/asl.h>`, verbatim fields + a kernel-private
tail for the config):

```c
struct FileRequester {       // ASL_FileRequest
    APTR   rf_Reserved1;
    BYTE   rf_Reserved2;
    APTR   rf_Reserved3;
    STRPTR rf_Dir;           // the chosen drawer (NUL-terminated)
    STRPTR rf_File;          // the chosen filename
    STRPTR rf_Pat;           // pattern (v0 unused)
    LONG   rf_LeftEdge, rf_TopEdge, rf_Width, rf_Height;
    WORD   rf_NumArgs;       // multi-select count (v0: 0/1)
    APTR   rf_ArgList;       // multi-select list (v0: nullptr)
    APTR   rf_UserData;
    // … (V38 fields declared for ABI; kernel-private config tail follows)
};

struct FontRequester {       // ASL_FontRequest
    APTR   fo_Reserved1;
    struct TextAttr fo_Attr; // the chosen font
    BYTE   fo_FrontPen, fo_BackPen, fo_DrawMode;
    APTR   fo_UserData;
    // …
};

struct ScreenModeRequester { // ASL_ScreenModeRequest (V38)
    ULONG  sm_DisplayID;
    UWORD  sm_DisplayWidth, sm_DisplayHeight, sm_DisplayDepth;
    APTR   sm_UserData;
    // …
};
```

Request-type codes: `ASL_FileRequest 0`, `ASL_FontRequest 1`,
`ASL_ScreenModeRequest 2`. CaraOS hangs the kernel-private config (the
parsed `ASLFR_*`/`ASLFO_*` tags: title, initial drawer/file, parent
window) off a sibling struct keyed by the requester pointer (or a tail
past the public fields), never inside the ABI struct's public region.

### 2.4 `AslRequest` — the modal run (build + wait)

`AslRequest(req, tags)` merges the call-time tags over the alloc-time
config, builds the modal window for `req`'s type, runs the loop, and on OK
writes the result back into the requester struct (returning `TRUE`); on
Cancel returns `FALSE`. Split internally into **`Croi_Asl_Build` +
`Croi_Asl_Wait`** (the L5 requester seam) so a `KERNEL_TEST` can pre-post
an OK `IDCMP_GADGETUP` before the wait (no live input pump needed).

- **File** (`ASL_FileRequest`): a window titled `ASLFR_TitleText` with a
  drawer `STRING_KIND` (`ASLFR_InitialDrawer`), a file `STRING_KIND`
  (`ASLFR_InitialFile`), and OK/Cancel buttons. On OK, copy the two
  string buffers into `rf_Dir`/`rf_File` (shared-heap-owned, freed by
  `FreeAslRequest`). (LISTVIEW browse → §6.)
- **Font** (`ASL_FontRequest`): a `CYCLE_KIND` over the available faces
  (v0: the one Dath 8×8 face) + OK/Cancel. On OK, fill `fo_Attr` from the
  chosen face.
- **ScreenMode**: v0 fills the single display mode + returns TRUE (a
  trivial confirm dialog, or immediate success).

The window is parented to `ASLFR_Window`/`ASLFO_Window` (or centred on the
active screen if none).

### 2.5 Legacy V36 entry points

`AllocFileRequest()` = `AllocAslRequest(ASL_FileRequest, nullptr)`;
`FreeFileRequest(fr)` = `FreeAslRequest`; `RequestFile(fr)` = `AslRequest`
with the file-requester's stashed config. Thin wrappers, same impls.

---

## 3. LVO offsets (canonical — **verify at L9.1**)

From `asl_lib.fd` (reserved slots 0..3 = -6..-24; confirm against
`amiga_docs/` before declaring). All implemented rows `syscall`.

| LVO | offset | ord | slice |
|-----|--------|-----|-------|
| `AllocFileRequest` | -30 | 4 | L9.2 |
| `FreeFileRequest`  | -36 | 5 | L9.1 |
| `RequestFile`      | -42 | 6 | L9.2 |
| `AllocAslRequest`  | -48 | 7 | L9.1 |
| `FreeAslRequest`   | -54 | 8 | L9.1 |
| `AslRequest`       | -60 | 9 | L9.2 |

(`AllocAslRequestTags`/`AslRequestTags`/`FreeAslRequestTags` are the
varargs forms — CaraOS provides them as `<proto/asl.h>` header inlines
over the `…A`-style LVOs, like `OpenWindowTags` over `OpenWindowTagList`,
not as separate LVOs. Verify whether the V39 FD lists them as real LVOs
and adjust.) `##pad_run` fills gaps as usual.

---

## 4. Slice plan

- **L9.1 — library + alloc/free.** `asl.conf` + `AslBase` + boot
  construction; `<libraries/asl.h>` (the three requester structs +
  `ASL_*`/`ASLFR_*`/`ASLFO_*`/`ASLSM_*`). `AllocAslRequest`/
  `FreeAslRequest` (+ legacy `FreeFileRequest`), parsing/stashing the
  config tags. **Test:** alloc a file + a font requester, check the
  defaults, free.
- **L9.2 — the file requester.** `AslRequest` for `ASL_FileRequest` (the
  string-entry modal over the L5 requester core + gadtools STRING/BUTTON);
  `AllocFileRequest`/`RequestFile` legacy. **Test:** build + pre-post OK →
  `rf_Dir`/`rf_File` reflect the fields; Cancel → FALSE.
- **L9.3 — font + screen-mode.** `AslRequest` for `ASL_FontRequest`
  (CYCLE → `fo_Attr`) and `ASL_ScreenModeRequest` (single mode).
  **Test:** font requester OK → `fo_Attr` is the Dath face.

---

## 5. Testing

asl is `syscall`, so the impls are callable from the S-mode runner.
Exercise via `KERNEL_TEST` over `Croi_Asl_*_Impl` using the **L5
requester pre-post seam**: `Croi_Asl_Build` the requester window, pre-post
an OK (or Cancel) `IDCMP_GADGETUP` on its port, `Croi_Asl_Wait`, then
assert the result + the read-back fields — exactly the
`intuition_requester` pattern (no live input pump). A Gleas
(`userintuition.c` or a new one) can additionally drive the full
`OpenLibrary("asl.library")` → AllocAslRequest → AslRequest path.

---

## 6. Tracked gaps / deferrals

- **Browsing file list** (LISTVIEW of `Examine`/`ExNext` entries, drawer
  navigation, double-click pick) — gated on the deferred gadtools
  `LISTVIEW_KIND`; v0 is string-entry only.
- Multi-select (`ASLFR_DoMultiSelect`/`rf_ArgList`), pattern matching
  (`ASLFR_DoPatterns`), save-mode confirm, filter/hook funcs.
- Real font enumeration (needs `diskfont.library` + multiple faces) and
  the screen-mode database (Phase 4 RTG) — v0 offers the one face / mode.
- Requester window resize/persistence, the `ASLFR_*`/`ASLFO_*` tail tags
  beyond title/initial-path/parent-window.
