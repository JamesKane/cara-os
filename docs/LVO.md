# Library / driver bridge — the LVO-gen model

> Status: design draft. No code has been written against it yet.
> Companion to `docs/ARCHITECTURE.md` §7 (libraries and LVOs).
> Answers open question §14.4 (hot-path syscall encoding) and pins
> a per-LVO answer to §14.7 (driver model, U-mode vs S-mode).

This document specifies how a user program's
`OpenLibrary("graphics.library", 36)` followed by
`RectFill(rp, 0, 0, w, h)` is dispatched all the way to the
implementation — whether the implementation is in-process CPU
code, a Croi syscall, or a `PutMsg` round-trip to a U-mode
driver Gleas.

The credo is **one mechanism, used uniformly**.

---

## 1. Summary in three sentences

CaraOS dispatches every AmigaOS V36+ library function through a
**per-library function-pointer table**, populated when the library
is constructed and called via auto-generated inline stubs in the
public `<proto/*.h>` headers. The stub body is uniform — load the
library base, load the function pointer, jump — but the *target*
of each entry decides whether the function runs as in-process
code, an `ecall` into Croi, or a `PutMsg` round-trip to a U-mode
driver Gleas. The application is unaware of the choice; the
library's `.conf` spec declares it.

---

## 2. Why this shape

Three minimal-OS patterns informed the design:

- **AROS** abandoned executable LVO slots on non-68k targets
  (x86, x86_64, ARM). Each slot became a function-pointer entry
  read by stubs generated from a per-library `.conf` (`genmodule`).
  Their decision validates ours. RV64 has no clean 6-byte
  instruction encoding and an 8-byte pointer doesn't fit a 6-byte
  slot, so executable-trampoline LVO encoding is a non-starter
  for us anyway.
- **Genode** showed one declarative spec can generate both a
  local-call stub and a remote-IPC stub. The application sees one
  call shape; the framework decides whether the body marshals +
  sends or just forwards. We adopt spec-driven generation in C23
  form (no C++ templates).
- **MINIX 3 / seL4** confirmed that one IPC primitive used
  uniformly across kernel calls, user-mode services, and drivers
  is enough. CaraOS's `PutMsg` over an SPSC ring (ARCHITECTURE.md
  §6) is that primitive. The library bridge does not introduce a
  second IPC mechanism.

What we did *not* lift: AROS's `__AROS_USE_FULLJMP` / `SetFunction`
patching machinery (SASOS removes the proxy-base problem they were
solving); seL4-style capability marshalling (Handles never cross
the API boundary — ARCHITECTURE.md §5.2); per-component IDL files
(C23 prototypes in the generated headers already typecheck the
marshal / demarshal).

---

## 3. Library memory layout

A CaraOS library on disk and in memory keeps the classic two-sided
shape: function-pointer table grows downward from the library base,
public `struct Library` + library-private state grows upward.

```
high                      :
addr                      :
                          +-----------------------+
                          | (library-private)     |
                          |   server MsgPort *    |
                          |   per-library state   |  positive
                          +-----------------------+  side
                          | struct Library        |  ▲
libBase ─────────────────▶|   lib_Node            |  │  matches V36+
                          |   lib_Flags  lib_pad  |  │  exactly,
                          |   lib_NegSize         |  │  field-for-field
                          |   lib_PosSize         |  │
                          |   lib_Version         |  │
                          |   lib_Revision        |  │
                          |   lib_IdString        |  │
                          |   lib_Sum             |  │
                          |   lib_OpenCnt         |  ▼
                          +-----------------------+  ▲  CaraOS
                          | vec[N-1]: void *      |  │  function-
                          | vec[N-2]: void *      |  │  pointer
                          |  ...                  |  │  table
                          | vec[1]:    void *     |  │  (negative
                          | vec[0]:    void *     |  │  side)
                          +-----------------------+  ▼
low                       :
addr                      :
```

`vec[i]` is the function pointer for runtime ordinal `i`. It lives
at `libBase - sizeof(void *) * (i + 1)` — i.e. `libBase - 8` is
`vec[0]`, `libBase - 16` is `vec[1]`, on RV64.

**Reserved indices** mirror V36+ `<exec/libraries.h>`:

| Index | Role         | V36+ canonical LVO |
|-------|--------------|--------------------|
| 0     | LIB_OPEN     | -6                 |
| 1     | LIB_CLOSE    | -12                |
| 2     | LIB_EXPUNGE  | -18                |
| 3     | LIB_EXTFUNC  | -24 (must be NULL) |
| 4..N  | user-defined | -30 onward         |

The CaraOS runtime ordinal is **declaration order** in the
library's `.conf`. The ordinal-to-V36+-LVO mapping is preserved as
data (for the Phase 9 binary translator and for documentation), but
**the runtime never indexes by negative byte offset** — the V36+
numbers are not the physical offsets on RV64.

### 3.1 Why we do not preserve the 6-byte stride at runtime

ARCHITECTURE.md §7.2 currently says "the slot stride is 6 bytes —
the same `LIB_VECTSIZE` constant from `<exec/libraries.h>`". That
stride was a 68k-era artefact: a `JMP abs.L` instruction is exactly
6 bytes. On RV64 the stride buys nothing — pointers are 8 bytes
and instructions don't pack to 6. The header constant
`LIB_VECTSIZE` stays at 6 *for source-level V36+ compatibility*
(programs that compile-time-reference it for documentation); the
runtime stride is `sizeof(void *) = 8`.

This is an **explicit deviation from the V36+ binary ABI** that has
no source-level consequence. Programs that read `LIB_VECTSIZE` as a
constant in C source see 6, as the V36+ headers print. Programs
that *programmatically index the LVO table by `lib_NegSize / 6`*
break — and there are essentially none of those in well-written
V36+ source. Phase 9's 68k translator handles the offset
translation as a lookup, not as arithmetic.

If a future hardware-banger 68k binary needs the literal 6-byte
stride, it lives in the Phase 9 translator's emulated 68k memory
view, not in the CaraOS runtime.

---

## 4. The `.conf` format

One declarative spec per library lives in `tools/lvo-gen/<libname>.conf`.
Format: directive lines (start with `##`), comment lines (start
with `#`), and function lines (everything else). Whitespace
between fields is one or more spaces / tabs.

### 4.1 Header directives

```
##library    graphics.library         # filename in 0x4000_0000 region
##base       GfxBase                  # global C variable name
##base_type  GfxBase                  # struct type the base points at
##bias       30                       # first user-defined LVO offset
##spdx       BSD-2-Clause             # for the generated headers
```

### 4.2 Function lines

```
# name        return        args                                                    lvo    flavour    impl-symbol
RectFill      void          (rp:RastPort*, x1:LONG, y1:LONG, x2:LONG, y2:LONG)      -306   local      Dath_RectFill
Move          void          (rp:RastPort*, x:LONG, y:LONG)                          -240   local      Dath_Move
BltBitMap     ULONG         (src:BitMap*, sx:LONG, sy:LONG, dst:BitMap*, dx:LONG, dy:LONG, w:LONG, h:LONG, minterm:UBYTE, mask:UBYTE, tmp:APTR)   -30    server     Dath_Server::BltBitMap
LoadView      void          (view:View*)                                            -222   server     Dath_Server::LoadView
```

Fields, left to right:

1. **name** — verbatim AmigaOS V36+ spelling. Becomes the public
   symbol the application calls.
2. **return** — return type, AmigaOS-namespace.
3. **args** — parenthesised, comma-separated `name:type` pairs.
   Empty `()` for nullary.
4. **lvo** — V36+ canonical LVO offset (negative integer). The
   generator validates `lvo == -(bias + ordinal_within_user_space * 6)`,
   skipping ordinals that map to declared `_PADn` slots. Documents
   the V36+ wire identity even though it isn't the physical offset
   at runtime.
5. **flavour** — one of `local`, `syscall`, `server`. See §5.
6. **impl-symbol** — for `local` and `syscall`, the C symbol the
   stub points at (resolved at link time). For `server`, the
   form `<gleas-name>::<message-tag>` names the receiving Gleas
   and the message kind.

### 4.3 Padding

V36+ libraries occasionally leave an LVO slot reserved (`_PADn`,
unused). The `.conf` declares these explicitly so the ordinal
sequence stays aligned with the canonical LVO arithmetic:

```
_PAD          void          ()                                                      -288   local      Croi_LvoUnimplemented
```

The default `Croi_LvoUnimplemented` returns zero and logs a
`LVO_UNIMPL` warning to the system log.

### 4.4 Pragmas, options, save-bases

V36+ Lattice / SAS-C used `#pragma libcall` directives that encoded
the calling-convention (which 68k registers carried which arg).
CaraOS does not preserve any of that. Argument lowering uses the
RV64 LP64D ABI verbatim, with an implicit final argument: a
pointer to the library base, passed as the last positional
argument. (This is what V36+ did with A6, made explicit in the
RV64 calling convention.)

---

## 5. The three implementation flavours

Each LVO's `flavour` field decides what `vec[ordinal]` actually
points at when the library is constructed. The application sees
one call shape regardless.

### 5.1 `local` — pure in-process call

`vec[ordinal]` is the address of a normal C function in the
library's shared RX page (mapped at `0x4000_0000+`, ARCHITECTURE.md
§4.3). A `local`-flavoured LVO is the cheapest dispatch:

- Application: `RectFill(rp, x1, y1, x2, y2)`
- Inline stub: load `GfxBase`, load `vec[IDX_RectFill]`, jalr.
- Body: `Dath_RectFill` is a plain C function, takes the explicit
  `GfxBase *` last argument, reads the RastPort, writes pixels into
  the BitMap's SASOS-resident framebuffer. Returns.

No syscall, no IPC. The CPU rasteriser already shipped in Phase 1
(`Dath_FillRect` etc.) exposes its surface this way once the
library trampolines land.

Use for: any function whose state is reachable from its argument
pointers under SASOS, and that doesn't need privileged access.

### 5.2 `syscall` — `ecall` into Croi

`vec[ordinal]` is the address of a tiny `ecall` stub also in the
library's shared RX page. The stub:

```
li      a7, SYS_AllocMem      # syscall number from include/cara/sysno.h
ecall                          # trap to Croi
ret
```

Croi's syscall dispatcher (already present from Phase 1 Epic K)
routes by `a7`, runs the kernel-internal implementation
(`Croi_AllocMem_Impl`), returns in `a0`.

Use for: anything Croi owns — `AllocMem`, `FreeMem`, `Wait`,
`Signal`, `AllocSignal`, `OpenLibrary`, `CloseLibrary`, `PutMsg`,
`GetMsg`, `WaitPort`, `OpenDevice`, `DoIO`. These are exec.library's
core. The hot path collapses to one trap.

ARCHITECTURE.md §14.4 asked: ecall + selector vs LVO-style trapped
jump? **The answer is "ecall + selector, one level of indirection
from the LVO table."** The LVO table holds the syscall stub's
address; the stub holds the syscall number. We get the unified
dispatch shape (always a fnptr call) without paying a per-LVO
trap-handler decode.

### 5.3 `server` — `PutMsg` round-trip to a Gleas

`vec[ordinal]` is the address of a server stub in the library's
shared RX page. The stub:

1. Allocates (or borrows from a per-task pool) a `struct Message`
   payload sized for this LVO's argument signature.
2. Marshals arguments into the payload.
3. Reads the library-private MsgPort pointer (stored in the
   library-private state past the public `struct Library` prefix
   — see §3 layout).
4. `PutMsg(server_port, &msg)`.
5. `WaitPort(reply_port)` on the calling task's per-task reply port.
6. Demarshals the return value and returns it.

The receiving Gleas (e.g. the Dath driver Gleas for
`graphics.library` server-flavour LVOs) is a normal U-mode task
that owns the hardware MsgPort, dispatches incoming messages by
`mn_Length`-prefixed kind tag, runs the implementation, replies.

Use for: anything that needs serialisation against a hardware
resource (GPU command ring, NVMe submission queue, USB endpoint),
and any classic AmigaOS *device* (`audio.device`, `console.device`,
`serial.device`, `timer.device` — these are already MsgPort-shaped
in the V36+ contract via `OpenDevice` / `DoIO`).

ARCHITECTURE.md §14.7 asked: do drivers run as Gleasanna or as
Croi-linked S-mode modules? **The answer is per-LVO: the `.conf`
flavour decides.** A library can mix `local`, `syscall`, and
`server` LVOs freely. `graphics.library`'s `RectFill` on a CPU
BitMap is `local`; its `BltBitMap` to a GPU friend bitmap is
`server`. One library, two flavours. The application doesn't see
the seam.

### 5.4 Choosing a flavour

| Criterion                                 | Flavour    |
|-------------------------------------------|------------|
| Pure data manipulation, args carry state  | `local`    |
| Reads / mutates kernel-owned state        | `syscall`  |
| Needs serialisation against a hardware-owning Gleas | `server` |
| Devices behind `OpenDevice`/`DoIO`        | `server`   |
| Tag-list helpers, list-walking, math      | `local`    |
| Memory allocation, signals, ports         | `syscall`  |

When in doubt, prefer `local`. SASOS lets a surprising number of
operations live in-process safely — anywhere a function's state is
reachable from its argument pointers under read/write caps the
caller already holds.

---

## 6. The application-facing stub

`tools/lvo-gen` emits one inline stub per LVO into
`<proto/<libname>.h>`. The shape, for a hypothetical `RectFill`:

```c
// SPDX-License-Identifier: BSD-2-Clause
// AUTOGENERATED by tools/lvo-gen from graphics.conf; do not edit.

#include <exec/libraries.h>
#include <graphics/gfxbase.h>
#include <graphics/lvo.h>

extern struct GfxBase *GfxBase;

[[gnu::always_inline]]
static inline void RectFill(struct RastPort *rp,
                            LONG x1, LONG y1, LONG x2, LONG y2)
{
    typedef void (*F)(struct RastPort *, LONG, LONG, LONG, LONG,
                      struct GfxBase *);
    F fn = (F)((void **)GfxBase)[-1 - CARA_IDX_RectFill];
    fn(rp, x1, y1, x2, y2, GfxBase);
}
```

Three RV64 instructions on the hot path: load `GfxBase`, load fnptr
at `[GfxBase - 8 * (IDX_RectFill + 1)]`, `jalr`. The
`[[gnu::always_inline]]` ensures no extra prologue / epilogue at
the application call site.

The library-base global (`GfxBase`, `SysBase`, `IntuitionBase`,
`DOSBase`, …) is the V36+ convention preserved verbatim: each
program declares `struct GfxBase *GfxBase;` and assigns to it from
`OpenLibrary`. No reserved register, no compiler magic — clang's
LP64D ABI handles register allocation around the call.

> Note. An earlier sketch proposed pinning `s11` as a reserved
> "current libbase" register à la 68k A6. Dropped: the V36+ C
> convention puts the base in a normal global, and the per-call
> load is one instruction. Reserving a callee-saved register
> would add ABI surface for zero measured win.

### 6.1 LVO ordinals header

`<graphics/lvo.h>`:

```c
// SPDX-License-Identifier: BSD-2-Clause
// AUTOGENERATED by tools/lvo-gen from graphics.conf; do not edit.

#pragma once

// CaraOS runtime ordinals (declaration order in graphics.conf).
constexpr int CARA_IDX_Open       =  0;
constexpr int CARA_IDX_Close      =  1;
constexpr int CARA_IDX_Expunge    =  2;
constexpr int CARA_IDX_ExtFunc    =  3;
constexpr int CARA_IDX_BltBitMap  =  4;
// ...
constexpr int CARA_IDX_RectFill   = 50;
// ...

// V36+ canonical LVO offsets — Phase 9 lookup keys, NOT physical
// memory offsets at runtime. See docs/LVO.md §3.1.
constexpr int _LVOOpen      =  -6;
constexpr int _LVOClose     = -12;
constexpr int _LVOExpunge   = -18;
constexpr int _LVOExtFunc   = -24;
constexpr int _LVOBltBitMap =  -30;
// ...
constexpr int _LVORectFill  = -306;
// ...
```

Both sets of constants come out of the same `.conf` row.

---

## 7. The library construction

`tools/lvo-gen` also emits a per-library construction source file
with the function-pointer table populator and the `MakeLibrary`-time
hook list:

```c
// src/dath/graphics_vec.c — AUTOGENERATED
// SPDX-License-Identifier: BSD-2-Clause

#include <graphics/lvo.h>
extern void Dath_Open(void);
extern void Dath_Close(void);
extern void Dath_Expunge(void);
// ...
extern void Dath_RectFill(struct RastPort *, LONG, LONG, LONG, LONG,
                          struct GfxBase *);
// ...
extern void Dath_Server_BltBitMap_Stub(/* server-flavour stub */);
// ...

void *graphics_lib_vec[] = {
    [CARA_IDX_Open]      = (void *)Dath_Open,
    [CARA_IDX_Close]     = (void *)Dath_Close,
    [CARA_IDX_Expunge]   = (void *)Dath_Expunge,
    [CARA_IDX_ExtFunc]   = nullptr,
    [CARA_IDX_BltBitMap] = (void *)Dath_Server_BltBitMap_Stub,
    // ...
    [CARA_IDX_RectFill]  = (void *)Dath_RectFill,
    // ...
};
constexpr size_t graphics_lib_vec_count =
    sizeof(graphics_lib_vec) / sizeof(graphics_lib_vec[0]);
```

The library's load-time `MakeLibrary` (Phase 3 surface, kernel-side
internal `Croi_MakeLibrary`) takes:

- The address of the upper-side `struct Library` prefix.
- The vec table above.
- Library-private state size and initialiser.

It allocates the negative-side region of the library image, copies
the vec table into it (in *reverse* — `vec[0]` at `libBase - 8`,
`vec[N-1]` at `libBase - 8*N`), fills `lib_NegSize = 8 * N`, fills
`lib_PosSize = sizeof(struct Library) + private_size`, and
registers the library with Croi for `OpenLibrary` lookup.

For server-flavour LVOs, the `MakeLibrary` step also wires the
library-private MsgPort pointer to the receiving Gleas's port (by
Kobj id, looked up at construction time).

---

## 8. The Phase 9 binary translator hook

`tools/lvo-gen` emits a third artefact: an aggregated
`(library, lvo_offset) → impl_symbol` lookup table for the Phase 9
68k → RV64 translator. When a translated 68k program executes
`JSR -198(A6)` with A6 holding the SASOS-resident `SysBase`, the
translator:

1. Recognises the LVO pattern in the 68k decode.
2. Looks up `("exec.library", -198)` in the aggregated table.
3. Emits a direct RV64 call to `Croi_AllocMem_Impl` (or to the
   syscall stub, equivalently — the translator can pick the
   shortest path per LVO).

This is *the* technically interesting content of Phase 9
(ROADMAP.md §9). The `.conf` files are its declarative input.

The aggregated table lives at
`include/aistreoir/lvo_table.gen.h` and is generated alongside the
public headers.

---

## 9. Worked example — the user's snippet end-to-end

```c
struct GfxBase *gfxBase;
gfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 0);
if (gfxBase) {
    UWORD version  = gfxBase->LibNode.lib_Version;
    UWORD revision = gfxBase->LibNode.lib_Revision;
    printf("graphics.library: %d.%d\n", version, revision);
    CloseLibrary((struct Library *)gfxBase);
}
```

Step by step on CaraOS:

1. `OpenLibrary("graphics.library", 0)` lowers to the inline stub
   from `<proto/exec.h>`. The stub loads `SysBase` (a global,
   set by `libcara` startup before `main`), reads
   `vec[CARA_IDX_OpenLibrary]` from `SysBase - 8 * (IDX + 1)`,
   `jalr`s. The vec entry is an `ecall` stub
   (`flavour = syscall`). Croi looks up `"graphics.library"` in
   the library registry, ensures the library's pages are mapped
   into the calling task's lower-half view (always already true —
   the `0x4000_0000` shared library region is RX in every Gleas),
   atomic-bumps `lib_OpenCnt`, returns the SASOS pointer to
   `gfxBase`.
2. `gfxBase->LibNode.lib_Version` is a **plain memory load** from
   the shared RX page. No syscall, no IPC. SASOS: the pointer is
   identical in every Gleas's address space, the page is mapped,
   the field is at a known offset within `struct Library`. This
   is the design's keystone payoff — the V36+ idiom of "just
   dereference the base" survives unchanged.
3. `printf` is a libcara function backed by `Croi_Log` via a
   `syscall`-flavoured exec.library extension LVO; not in the
   user's code path strictly, but worth noting it follows the
   same model.
4. `CloseLibrary(gfxBase)` lowers identically to step 1: stub,
   `vec[CARA_IDX_CloseLibrary]`, ecall, atomic-decrement, return.

Touch count for this snippet:

- 2 syscalls (Open, Close).
- 0 driver server hops.
- 1 plain memory read of two adjacent half-words.

The graphics-server (Dath driver Gleas) is **not on this path**.
It engages only when the program calls a server-flavoured LVO
like `BltBitMap` to a GPU friend bitmap.

---

## 10. The `tools/lvo-gen` design

### 10.1 Inputs

```
tools/lvo-gen/
├── lvo-gen.c                   # the tool itself, C23, host-built
├── exec.conf
├── dos.conf
├── intuition.conf
├── graphics.conf
├── utility.conf
├── gadtools.conf
├── asl.conf
├── iffparse.conf
├── commodities.conf
├── icon.conf
├── diskfont.conf
├── expansion.conf
├── keymap.conf
├── layers.conf
├── workbench.conf
└── ...
```

The `.conf` files are the canonical CaraOS LVO truth. Manual
editing of generated headers is forbidden; their first line says
so.

### 10.2 Outputs

For each `<libname>.conf`:

- `include/proto/<libname>.h` — inline stubs (one per LVO).
- `include/<libname>/lvo.h` — `CARA_IDX_*` and `_LVO*` constants.
- `src/<owner>/<libname>_vec.c` — function-pointer table source.
- contributions to `include/aistreoir/lvo_table.gen.h` — Phase 9
  lookup table.

The owner directory is read from a `##owner` directive in the
`.conf` (e.g. `##owner croi/exec_lib` for exec.library,
`##owner dath` for graphics.library).

### 10.3 Generation pass

```
parse(<libname>.conf)
  -> headers + vec_source + lvo_table_fragment

aggregate(all *.conf)
  -> include/aistreoir/lvo_table.gen.h
```

The tool is single-file C23, no third-party deps (PRINCIPLES.md
§2). It reads `.conf` line-by-line, validates LVO arithmetic
against the bias and stride, and writes the four artefact families
above. Run from CMake as a `add_custom_command` step before the
`exec.library`, `graphics.library`, etc. targets compile.

### 10.4 Validation

`lvo-gen` enforces:

- Declaration order matches `lvo` ordering (each row's `lvo`
  monotonically decreases by `LIB_VECTSIZE = 6`, with explicit
  `_PAD` rows for unused canonical slots).
- Reserved indices 0..3 contain `Open`, `Close`, `Expunge`, and
  `_PAD` (or `ExtFunc` returning zero) in that order, regardless
  of how the `.conf` orders them.
- Every public name is unique within the library.
- Every implementation symbol resolves at link time (best-effort:
  the tool only checks the name shape; the linker is the final
  arbiter).
- Server-flavour symbols match `<gleas>::<tag>` syntax.

---

## 11. What this changes

### 11.1 Doc updates

- `docs/ARCHITECTURE.md` §7.1 should soften "single jalr through a
  thunk, or a direct tail-call sequence chosen by the linker
  script" to "function-pointer dispatch via stubs from
  `tools/lvo-gen`; canonical V36+ LVO numbers are header constants
  and Phase 9 lookup keys, not runtime memory offsets."
- `docs/ARCHITECTURE.md` §7.2 should reframe the 6-byte stride
  paragraph per §3.1 of this doc — the stride is a header / 68k
  concept, not a runtime constraint.
- `docs/ARCHITECTURE.md` §14.4 (open question — hot-path syscall
  encoding) is answered: function-pointer call where the target
  decides. Mark closed.
- `docs/ARCHITECTURE.md` §14.7 (open question — drivers in U-mode
  vs S-mode) is answered per-LVO via the `.conf` flavour. Not
  fully closed, since "which flavour for which LVO" is still a
  per-library design call, but the *mechanism* is settled. Update
  to "per-LVO via tools/lvo-gen `.conf`; see docs/LVO.md."

### 11.2 Drift fixes that this design unblocks

From `docs/DRIFT_2026-05.md`:

- **M3** (`<exec/types.h>` UBYTE/UWORD/ULONG/APTR/BPTR) — needed
  before any `.conf` row's argument types can be lowered.
- **M4** (`<exec/libraries.h>` `struct Library`) — needed before
  any library base can be returned from `OpenLibrary` and read
  into `gfxBase->LibNode.lib_Version` by a user program.
- **M1** (`<exec/memory.h>` MEMF_*) and **M2** (Wait/Signal
  trampoline definitions) — also unblocked, since the `.conf`
  rows for `AllocMem`, `Wait`, `Signal`, etc. can be written
  immediately.

### 11.3 First library to drive through the tool

`exec.library`, because:

- `OpenLibrary` itself lives there. Bootstrap.
- `AllocMem` / `FreeMem` / `Wait` / `Signal` / `PutMsg` / `GetMsg`
  / `WaitPort` are all `syscall`-flavoured and trivial to wire to
  existing Phase 1 Croi internals.
- It has zero `server`-flavoured LVOs (devices live in
  `*.device`, not exec).
- Lighting up exec.library makes the user's worked example in §9
  compile and run end-to-end.

Once exec.library works, `graphics.library` is next — it's the
first library to exercise both `local` (CPU rasteriser path on
the existing Phase 1 Dath surface) and `server` (Dath Gleas
ownership of the GPU command ring, when Phase 4 lands).

---

## 12. Open questions (smaller now)

1. **`MakeLibrary` API.** Phase 3's public `MakeLibrary` is the
   V36+ contract; CaraOS's internal helper is
   `Croi_MakeLibrary`. The exact shape of the construction
   parameter struct is not yet pinned. Likely: a tagged-list of
   `(vec_table, vec_count, struct_size, init_routine,
   server_port_kobj_id_or_zero)`.
2. **Server-flavour Message marshalling.** Each server LVO's
   `struct Message`-prefixed payload layout is generated from the
   `.conf` argument list; field packing rules need to be pinned.
   Proposed: each arg is its natural-sized aligned field in
   declaration order, no padding except what alignment demands.
   Pointer arguments are SASOS pointers passed verbatim.
3. **Reply correlation under multi-message dispatch.** A server
   stub `PutMsg`s, then `WaitPort`s. If the calling task has
   multiple in-flight server calls (it shouldn't on the synchronous
   path, but the contract should be explicit), correlation needs
   a token. v0: synchronous only — the stub `WaitPort`s for the
   reply to the message it just sent and asserts identity.
4. **Per-task reply port.** Each Gleas needs one reply MsgPort
   used by all server-flavour stubs across all libraries. Owned
   by libcara, created at task startup, freed at task exit.
   Single port suffices for v0 because dispatch is synchronous.
5. **`OldOpenLibrary` (LVO -408) vs `OpenLibrary` (LVO -552).**
   The V36+ ABI keeps both; `OldOpenLibrary` ignores the version
   argument. Both reduce to the same `Croi_OpenLibrary_Impl`
   syscall with `version = 0` for the old form. Trivial; called
   out only because `lvo-gen` will see two rows pointing at one
   implementation symbol, and the validator must allow this.
6. **Hosted unit-testing of the generated stubs.** Running
   `lvo-gen` on a tiny hand-rolled `.conf` and link-testing the
   result against a stub library that records calls. Before any
   real exec.library code lands.

---

## 13. Glossary delta

These terms enter the project with this design:

| Term                | Meaning                                                                                  |
|---------------------|------------------------------------------------------------------------------------------|
| **vec table**       | Per-library `void *` array indexed by runtime ordinal; lives at the negative side of `libBase`. |
| **runtime ordinal** | CaraOS's per-library declaration-order index (0..N-1). What the inline stubs use.        |
| **canonical LVO**   | V36+ negative byte offset (`-30`, `-198`, …). Header constant + Phase 9 lookup key. Not a runtime memory offset on RV64. |
| **flavour**         | One of `local`, `syscall`, `server`. Set per-LVO in the `.conf`. Decides what `vec[ordinal]` points at. |
| **server stub**     | Auto-generated marshal+`PutMsg`+`WaitPort`+demarshal wrapper for a `server`-flavoured LVO. Lives in the library's shared RX page. |
| **Gleas server**    | A U-mode driver task (e.g. the Dath Gleas) that owns a hardware resource and hosts a MsgPort receiving server-flavoured messages from libraries. |

---

## See also

- `docs/ARCHITECTURE.md` §4 (SASOS), §5 (Kobj / Handles), §6 (IPC
  rings), §7 (libraries and LVOs).
- `docs/PRINCIPLES.md` §3.1 (brand-vs-API namespace split).
- `docs/PHASE1_RUNTIME.md` (the Phase 1 internals the first
  syscall-flavoured LVOs trampoline into).
- `docs/DRIFT_2026-05.md` M1–M4 (gaps this design unblocks).
- `docs/ROADMAP.md` Phase 3 (the surface this design is the
  bridge to) and Phase 9 (the binary translator that consumes the
  aggregated LVO lookup table).
