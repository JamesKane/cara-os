# CaraOS FDT (device-tree) parser — design and scope

> Scope doc. Pairs with `docs/ARCHITECTURE.md` §9. No code yet — this
> document specifies what the parser must do, what it must *not* do, the
> public API contract, the FDT format subset we cover, and the test plan.

---

## 1. Goals

1. **One source of truth for hardware addresses.** Splanc and Croi both
   discover MMIO bases, IRQ numbers, memory banks, and per-hart
   capabilities by walking the FDT handed to us by U-Boot. No CaraOS
   `.c` file embeds a board-specific constant.
2. **Same Croi binary boots QEMU `virt` and the OrangePi RV2** without a
   recompile or a CONFIG flag. The discriminator is the FDT.
3. **Read-only and allocation-free.** The parser may be called before the
   physical frame allocator exists. It must never modify the blob and
   must never call `Croi_Alloc`.
4. **Host-buildable.** The same `.c` files compile under the hosted CMake
   build and are exercised against captured DTBs in `tests/data/`. No
   target-specific intrinsics in the parser.
5. **Cleanroom from the *Devicetree Specification* (release 0.4 or
   later).** We do not vendor `libfdt`. `libfdt` remains a useful
   reference impl for cross-checking results during testing but is not
   linked into CaraOS.

## 2. Non-goals

- **No DTS source parsing.** We parse the *binary* FDT (DTB) only. The
  text DTS form is a vendor build artefact; we never see it on the
  target. (Tests may mention DTS for human readability, but always
  with the corresponding compiled DTB.)
- **No FDT mutation.** No `fdt_setprop`, no `fdt_add_subnode`, no
  `fdt_pack`. If a future need arises (e.g. patching `chosen` before
  passing to a sub-OS), build a separate writer; do not bolt mutation
  onto this parser.
- **No `phandle` *resolution* in v0.** We parse `phandle` *values*
  (they're just `u32` properties) and let callers cross-reference them,
  but a `phandle` → node lookup table is out of scope for v0. Add when
  the first driver needs it.
- **No clock-graph traversal in v0.** The parser exposes raw `clocks`
  property cells; computing the actual clock frequency for a given
  consumer is the job of a clock module that doesn't exist yet.

## 3. The DTB format we cover

Reference: *Devicetree Specification* v0.4, sections 5.1–5.4. Summary in
this doc is not authoritative; the spec is.

### 3.1 Header

```c
struct FdtHeader {                  /* all fields big-endian */
    uint32_t magic;                 /* 0xD00DFEED */
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;               /* require >= 17 */
    uint32_t last_comp_version;     /* require <= 16 */
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};
```

`Fdt_Open` validates: magic, version range, that all four offset/size
pairs lie within `totalsize` and don't overlap, and that
`off_dt_struct + size_dt_struct <= totalsize`.

### 3.2 Memory reservation block

A 16-byte-aligned array of `(u64 address, u64 size)` pairs, terminated by
`(0, 0)`. CaraOS treats every entry here as off-limits to the physical
frame allocator, in addition to anything in `/reserved-memory`.

API: `Fdt_RsvIter(const struct Fdt *, uint32_t *cursor, uint64_t *base, uint64_t *size)`.

### 3.3 Structure block

A stream of 32-bit big-endian tokens:

| Token         | Value      | Payload                                     |
|---------------|------------|---------------------------------------------|
| `FDT_BEGIN_NODE` | `0x00000001` | NUL-terminated node name, padded to 4 B  |
| `FDT_END_NODE`   | `0x00000002` | (none)                                   |
| `FDT_PROP`       | `0x00000003` | `u32 len`, `u32 nameoff`, `len` bytes pad to 4 B |
| `FDT_NOP`        | `0x00000004` | (none) — must be skipped                 |
| `FDT_END`        | `0x00000009` | (none) — terminates the structure block  |

Walks are recursive: each `BEGIN_NODE` opens a child scope until the
matching `END_NODE`. `NOP` tokens may appear anywhere and are silently
skipped.

### 3.4 Strings block

A flat byte array of NUL-terminated strings; `nameoff` in `FDT_PROP`
indexes into it. The parser never copies these strings — it returns
`const char *` pointers into the blob (see §4.2).

### 3.5 Property values

Property bytes are uninterpreted at the format level. Per-property type
is determined by the binding (the `compatible` string + property name).
The parser provides typed accessors for the shapes CaraOS uses:

- `u32`           — `Fdt_PropU32`
- `u64`           — `Fdt_PropU64`
- string          — `Fdt_PropStr`
- string list     — `Fdt_PropStrIter`
- `reg` cells     — `Fdt_PropReg(node, index, &base, &size)` —
  consults `#address-cells`/`#size-cells` of the *parent* (this is the
  fiddly bit; see §5).
- `compatible`    — `Fdt_NodeIsCompatible(node, "riscv,plic0")`
  (string-list match)

Anything more exotic (fully-decoded `interrupts-extended`, full `ranges`
translation) is left to callers reading raw bytes via
`Fdt_PropRaw(node, name, &len)`.

### 3.6 Inheritance: `#address-cells` and `#size-cells`

These properties on a parent node specify how many `u32` cells make up
the address and size portions of `reg` and `ranges` entries on each
child. They default to **2 and 1** respectively if absent — but the
*Devicetree Specification* is clear that real-world DTBs always set them
explicitly. The parser shall:

1. Track `#address-cells` and `#size-cells` per ancestor while walking.
2. On `Fdt_PropReg`, use the **parent's** values to decode (not the
   node's own — its own cells govern *its* children).

The X1's top-level sets `#address-cells = <2>; #size-cells = <2>;` so
all top-level `reg` entries are 4 cells (64-bit base, 64-bit size).

## 4. API in detail

Header: `include/cara/fdt.h`. All functions are `[[nodiscard]]` unless
they obviously cannot fail (the iter cursors).

### 4.1 Lifecycle

```c
struct Fdt {
    const uint8_t *blob;
    uint32_t       totalsize;
    uint32_t       off_struct, size_struct;
    uint32_t       off_strings, size_strings;
    uint32_t       off_rsvmap;
    uint32_t       version;
    uint32_t       boot_cpuid_phys;
};

[[nodiscard]] int Fdt_Open(struct Fdt *out, const void *blob);
```

`Fdt_Open` validates the header (magic, version 17, internal
offset/size sanity) and fills `*out`. Returns `0` on success, a negative
`CARA_E*` code on any malformed input. Never reads past `totalsize`.

There is no `Fdt_Close` — a `struct Fdt` is a non-owning view.

### 4.2 String returns

All `const char *` returned by the parser point **into the blob**. The
caller must not free them and must not use them after the blob has been
unmapped. CaraOS guarantees the blob remains mapped for the lifetime of
Croi (it lives at a fixed kernel-virtual address, never unmapped).

For strings the caller wants to keep across a config change, copy them.

### 4.3 Node lookup

```c
/* Returns a node offset (opaque u32) for the root, never fails. */
uint32_t Fdt_Root(const struct Fdt *);

/* Walk by full path, e.g. "/chosen", "/cpus/cpu@0", "/soc/serial@d4017000". */
[[nodiscard]] int Fdt_ResolvePath(const struct Fdt *, const char *path,
                                  uint32_t *node_off_out);

/* Find the first/next node whose `compatible` list contains `compat`. */
[[nodiscard]] int Fdt_FindByCompatible(const struct Fdt *, const char *compat,
                                       uint32_t *node_off_inout);
/*  - On first call set *node_off_inout = 0; receive offset of first match.
 *  - To continue, leave the offset in *node_off_inout and re-call.
 *  - Returns CARA_ENOENT when no further match exists.
 */

/* Iterate immediate children. Cursor zero-init for first call. */
[[nodiscard]] int Fdt_ChildIter(const struct Fdt *, uint32_t parent,
                                uint32_t *cursor_inout);
```

### 4.4 Property accessors

```c
[[nodiscard]] int Fdt_PropU32(const struct Fdt *, uint32_t node,
                              const char *name, uint32_t *out);

[[nodiscard]] int Fdt_PropU64(const struct Fdt *, uint32_t node,
                              const char *name, uint64_t *out);

[[nodiscard]] int Fdt_PropReg(const struct Fdt *, uint32_t node,
                              uint32_t index,
                              uint64_t *base_out, uint64_t *size_out);

[[nodiscard]] const char *Fdt_PropStr(const struct Fdt *, uint32_t node,
                                      const char *name);   /* nullptr on miss */

[[nodiscard]] int Fdt_PropStrIter(const struct Fdt *, uint32_t node,
                                  const char *name,
                                  uint32_t   *cursor_inout,
                                  const char **str_out);

[[nodiscard]] int Fdt_PropRaw(const struct Fdt *, uint32_t node,
                              const char *name,
                              const void **bytes_out, uint32_t *len_out);

[[nodiscard]] bool Fdt_NodeIsCompatible(const struct Fdt *, uint32_t node,
                                        const char *compat);
```

### 4.5 Memory reservations

```c
[[nodiscard]] int Fdt_RsvIter(const struct Fdt *, uint32_t *cursor_inout,
                              uint64_t *base_out, uint64_t *size_out);
```

Walks `/memreserve/` entries (the binary block from §3.2). To enumerate
`/reserved-memory`'s child nodes, callers walk that subtree with
`Fdt_ResolvePath("/reserved-memory")` + `Fdt_ChildIter`.

## 5. Pitfalls captured up-front

- **Endianness.** Every multi-byte field in the FDT is big-endian. On a
  little-endian RISC-V host (and our hosted x86_64/arm64 build), every
  multi-byte read goes through a swap helper. Use a single `be32toh`
  inline; do not sprinkle byte-swaps.
- **Alignment.** Tokens and 32-bit cells in the structure block are
  4-byte aligned. Names after `FDT_BEGIN_NODE` are NUL-terminated and
  padded up to 4. Property values are padded up to 4. The parser must
  enforce these on read or reject the blob.
- **Bounds.** Every read offset must be checked against `totalsize`,
  `off_struct + size_struct`, or `off_strings + size_strings` as
  appropriate. The parser must be safe against an adversarial blob —
  not because the X1 will hand us one, but because a corrupted blob
  during early bring-up should fault our parser and not the rest of the
  kernel.
- **`#address-cells` / `#size-cells` defaults.** If absent, the spec
  says default to 2 and 1. Real-world DTBs always set them at the
  root, but emit a `Croi_Warn` on default-fallback so we notice
  surprises.
- **Empty / zero-length properties.** Some boolean-style properties
  exist as "present, length 0" (e.g. `dma-coherent`). `Fdt_PropU32` on
  these returns `CARA_EINVAL`; use `Fdt_PropRaw` to detect presence.

## 6. Splanc / Croi consumption

Both Splanc and Croi link the same `fdt.o`. Splanc uses it during UEFI
boot to:

- locate `/memory@*` for the GetMemoryMap cross-check
- read `/chosen/stdout-path` so its early `Splanc_Print` lands on the
  right UART before ExitBootServices
- read CLINT/PLIC for the eventual page-table mapping window in the
  upper half

Croi uses it during early init to populate `struct CroiPlatform` (see
ARCHITECTURE §9.4). After that, no other Croi code calls into
`Fdt_*` except driver modules opening their own subtrees — and even
those should prefer reading `struct CroiPlatform` if their data is
already there.

## 7. Test plan

Hosted unit tests under `tests/unit/test_fdt_*.c`:

1. **Header validation.** Hand-crafted invalid blobs: bad magic, wrong
   version, internal offset overlap, truncated. Each must yield a
   specific negative return code, no crash, no out-of-bounds read.
2. **Token walk.** A minimal blob with one root node, one child, one
   property — verify every API entry point returns the expected value.
3. **Compatible search.** Captured X1 DTB in `tests/data/x1.dtb` (built
   via `dtc` from the linux-orangepi `orange-pi-6.6-ky` source — a
   build script under `tools/dtb-capture/` automates the regen):
   - find `"riscv,clint0"` → expect base `0xE400_0000`, size `0x10000`.
   - find `"riscv,plic0"` → expect base `0xE000_0000`, size `0x0400_0000`,
     `riscv,ndev = 159`, `riscv,max-priority = 7`.
   - resolve `/chosen/stdout-path = "serial0:115200n8"` → walk aliases
     → `uart0` at `0xD401_7000`, IRQ 42, `reg-shift = 2`,
     `reg-io-width = 4`.
4. **`reg` decoding under `#address-cells = 2; #size-cells = 2;`.**
   Cross-check `Fdt_PropReg(uart0, 0, &b, &s)` returns those values
   exactly.
5. **Multi-bank memory.** `/memory@0` (2 GiB) and `/memory@100000000`
   (2 GiB) both enumerate; total is 4 GiB.
6. **Reserved enumeration.** `/reserved-memory/dpu_reserved@2ff40000`
   appears with `reg = 0x2ff40000 / 0x000C0000`.
7. **Cross-check vs `libfdt`.** A build-time test harness (excluded
   from production binary) parses the same DTB with `libfdt` and
   asserts every property/node observable matches. This catches
   correctness drift cheaply during development.
8. **QEMU `virt` DTB.** A second captured DTB in `tests/data/qemu-virt.dtb`,
   produced by `qemu-system-riscv64 -machine virt -dump-dtb`. Same
   queries succeed but with different addresses. This is how we keep
   QEMU and X1 booting from the same Croi binary.

## 8. Out-of-scope (future work)

- `phandle` resolution table.
- `interrupts-extended` and `interrupt-map` decoding.
- `ranges` translation (bus-relative → host addresses).
- A *writer* for the case where Croi wants to fix up a sub-blob before
  handing it to a child OS.
- DTBO overlay application.

Each is its own design exercise and need not block v0.

## 9. References

- *Devicetree Specification* v0.4 (https://www.devicetree.org/specifications/) —
  authoritative.
- `libfdt` source in U-Boot — reference implementation, BSD-2-Clause.
- ARCHITECTURE.md §9 — how this parser fits the boot flow.
- HARDWARE_RV2.md — the concrete numbers a correctly-implemented parser
  must extract from the X1 DTB.
