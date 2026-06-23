# L14 — expansion.library (scope)

Cleanroom V36+ `expansion.library` for CaraOS Phase 3 — the last of the
L10–14 long tail. On AmigaOS expansion.library owns the **AutoConfig**
machinery: at boot it walks the Zorro bus, AutoConfigs each board, and
builds a list of **ConfigDev** structures (one per board: manufacturer,
product, board address + size); programs then `FindConfigDev` a board by
its IDs, and the boot path turns bootable boards into DOS BootNodes.

CaraOS has no Zorro bus. The reframe (`docs/PHASE3.md §4`: "expansion.
library (FDT-backed AutoConfig analogue)") is: **the boot PCIe inventory
discovered from the FDT *is* the AutoConfig chain.** Each device in
`g_pci_inv` (`struct PciInventory`, populated by `Croi_Pci_Init` from the
FDT) becomes a ConfigDev, so a V36 program can `FindConfigDev` the boot's
NVMe / xHCI controllers by their PCI vendor/product IDs.

As with commodities (L13), **no representative app needs it** — editor/
paint/file-manager list none of it. So expansion ships **ABI-complete +
the testable core** (the ConfigDev list + its population from the FDT/PCI
inventory) and **stubs the Zorro-only machinery** (board config registers,
expansion-memory allocation, the binding model) and the DOS boot-node path
(CaraFS already mounts over NVMe at boot — `docs/LOGAIC_BOOT.md` — not via
expansion BootNodes).

Read alongside: `include/cara/pci.h` (the `PciInventory`/`PciFunction`
source), `docs/COMMODITIES.md`/`docs/DISKFONT.md` (scope-doc shape + the
"ship the testable core, stub the rest" discipline), `docs/LVO.md`.

---

## 1. Scope

**In scope (gets a working body):**

- The `expansion.library` skeleton (base-ful `syscall` library, the icon/
  commodities recipe).
- The full V36+ ABI: `include/libraries/configvars.h` (struct ConfigDev /
  ExpansionRom / ExpansionControl) + `configregs.h` flags (CONFIGF_*/ERT_*)
  + the ExpansionBase.
- The **ConfigDev list** ops: `AllocConfigDev`, `FreeConfigDev`,
  `AddConfigDev`, `RemConfigDev`, `FindConfigDev`.
- The **FDT-backed AutoConfig population**: the ConfigDev list is built
  (lazily, on first access) from `g_pci_inv` — one ConfigDev per
  `PciFunction`, mapping `vendor_id → er_Manufacturer`, `device_id →
  er_Product`, `bar[0].base → cd_BoardAddr`, `bar[0].size → cd_BoardSize`.

**Out of scope (declared ABI-complete, defined stub):**

- **Zorro board config** — `ReadExpansionByte`/`ReadExpansionRom`/
  `WriteExpansionByte`/`ConfigBoard`/`ConfigChain`: there is no Zorro
  config space on RV2; the devices are already configured by the PCIe BAR
  allocator at boot.
- **Expansion-memory allocation** — `AllocBoardMem`/`FreeBoardMem`/
  `AllocExpansionMem`/`FreeExpansionMem` (Zorro slot space): N/A.
- **The binding model** — `ObtainConfigBinding`/`ReleaseConfigBinding`/
  `SetCurrentBinding`/`GetCurrentBinding`: no driver-matching framework.
- **DOS boot nodes** — `MakeDosNode`/`AddBootNode`/`AddDosNode`: CaraFS
  mounts over NVMe at boot directly; the expansion BootNode → DOS path is
  not how CaraOS boots.
- **`ExpansionControl`**.

**Done-bar:** a V36 program `OpenLibrary`s expansion.library and
`FindConfigDev(nullptr, vendor, product)` returns the ConfigDev for the
boot's NVMe (or xHCI) controller with the right `cd_BoardAddr`/`cd_BoardSize`
from its BAR; walking the list (`FindConfigDev(prev, -1, -1)`) visits every
boot PCIe device; `AllocConfigDev`/`AddConfigDev`/`RemConfigDev`/
`FreeConfigDev` maintain the list. ABI-complete; the Zorro/boot-node half
is stubbed.

---

## 2. The key decisions

### 2.1 The FDT/PCI inventory IS the AutoConfig chain

Classic expansion.library *performs* AutoConfig (poking Zorro config
registers to assign each board an address). CaraOS's PCIe BAR allocator
already did the equivalent at boot (`Croi_Pci_Init` sized + placed every
BAR, filling `g_pci_inv.func[]`). So expansion.library does not configure
anything — it **presents** the already-configured `g_pci_inv` as a
ConfigDev list. The list is built once, lazily, on the first ConfigDev op
(no `entry.c` ordering dependency; `g_pci_inited` is true well before any
library call): one `ConfigDev` per `PciFunction`, then `AddConfigDev`'d.

### 2.2 PciFunction → ConfigDev mapping

| ConfigDev / ExpansionRom field | from `PciFunction` |
|--------------------------------|--------------------|
| `cd_Rom.er_Manufacturer` (UWORD) | `vendor_id` |
| `cd_Rom.er_Product` (UBYTE) | `device_id & 0xFF` (lossy; PCI ids are 16-bit) |
| `cd_Rom.er_Type` | `ERTF_MEMLIST`-free new-board marker |
| `cd_BoardAddr` (APTR) | `bar[0].base` |
| `cd_BoardSize` (ULONG) | `bar[0].size` |
| `cd_Flags` | `CONFIGF_AUTOCONFIG` |

`er_Manufacturer` is a UWORD, so the 16-bit PCI vendor id maps exactly;
`er_Product` is a classic UBYTE so it takes the device-id low byte (noted
lossy — a CaraOS extension could widen it, but the ABI struct is frozen).
`FindConfigDev(old, manufacturer, product)` matches `er_Manufacturer` /
`er_Product` with `-1` as the wildcard (the V36 contract), starting from
`old->cd_NextCD` (or the list head when `old` is null).

### 2.3 `ConfigDev` carries its `ExpansionRom` by value

Per the V36 ABI, `struct ConfigDev` embeds `struct ExpansionRom cd_Rom` by
value (not a pointer) and links via `cd_NextCD`. So `AllocConfigDev` is one
shared-heap allocation; the populator fills `cd_Rom` in place. The global
list head is a `src/croi/expansion` static; `AddConfigDev` appends,
`RemConfigDev` unlinks, `FreeConfigDev` frees. (Same opaque-to-app,
kernel-owned-list pattern as the commodities CxObj tree.)

### 2.4 `syscall` flavour; list in the shared heap

expansion is base-ful `syscall` flavour (icon/commodities recipe): each
implemented LVO is a `Cara_Trampoline_Exp_*` trampoline → Croi →
`Croi_Exp_*_Impl`. The ConfigDev list lives in the SASOS shared heap so a
returned `struct ConfigDev *` is valid in the caller. The base global is
the verbatim **`ExpansionBase`**. Population reads `g_pci_inv` directly
(extern), so it works from a `KERNEL_TEST` (no Process needed).

---

## 3. LVO surface

Bias 30; reserved slots 0–3 are `local` hooks. **`expansion_lib.fd` is the
alphabetically-ordered FD** (a known quirk), so the ConfigDev list ops are
*scattered*, not contiguous — `##pad_run` covers the stubbed gaps between
them. Offsets are the canonical V36+ values, locked against `amiga_docs/`
when `tools/lvo-gen/expansion.conf` is written in L14.1 (cross-check, never
copy).

| LVO | offset | group | slice |
|-----|-------:|-------|-------|
| `AddConfigDev` | -30 | ConfigDev list | L14.1 |
| `AddBootNode` | -36 | boot node | **stub** |
| `AllocBoardMem` | -42 | Zorro mem | **stub** |
| `AllocConfigDev` | -48 | ConfigDev list | L14.1 |
| `AllocExpansionMem` | -54 | Zorro mem | **stub** |
| `ConfigBoard` | -60 | Zorro config | **stub** |
| `ConfigChain` | -66 | Zorro config | **stub** |
| `FindConfigDev` | -72 | ConfigDev list | L14.1 |
| `FreeBoardMem` | -78 | Zorro mem | **stub** |
| `FreeConfigDev` | -84 | ConfigDev list | L14.1 |
| `FreeExpansionMem` | -90 | Zorro mem | **stub** |
| `GetCurrentBinding` | -96 | binding | **stub** |
| `MakeDosNode` | -102 | boot node | **stub** |
| `ObtainConfigBinding` | -108 | binding | **stub** |
| `ReadExpansionByte` | -114 | Zorro config | **stub** |
| `ReadExpansionRom` | -120 | Zorro config | **stub** |
| `ReleaseConfigBinding` | -126 | binding | **stub** |
| `RemConfigDev` | -132 | ConfigDev list | L14.1 |
| `SetCurrentBinding` | -138 | binding | **stub** |
| `WriteExpansionByte` | -144 | Zorro config | **stub** |
| `AddDosNode` | -150 | boot node (V36) | **stub** |

So 5 implemented (the ConfigDev list), the rest defined stubs
(`Croi_LvoUnimplemented`) so a V36 program links.

---

## 4. Slice plan

### L14.1 — the whole thing (closes L14)

expansion's testable core is small + cohesive, so it is a single slice:

- `tools/lvo-gen/expansion.conf` (full surface, offsets locked) →
  `proto/expansion.h` / `expansion/lvo.h` / `expansion_vec.c`;
  `include/libraries/configvars.h` (struct ConfigDev / ExpansionRom /
  ExpansionControl) + `configregs.h` (CONFIGF_*/ERT_*) + ExpansionBase; the
  `src/croi/expansion` library (base, hooks, trampolines, MakeLibrary in
  `entry.c`, `KEEP(.lib_text.expansion)`, whole-archive, coverage wiring).
- `AllocConfigDev`/`FreeConfigDev`/`AddConfigDev`/`RemConfigDev`/
  `FindConfigDev` over the global ConfigDev list + the lazy FDT/PCI
  population (`§2.1`-`2.2`) reading `g_pci_inv`.
- **Test (KERNEL_TEST):** with the boot PCIe inventory live,
  `FindConfigDev(nullptr, -1, -1)` returns a non-null first device and the
  walk visits `g_pci_inv.n_funcs` devices; `FindConfigDev(nullptr,
  vendor, product)` finds a specific boot controller with `cd_BoardAddr`/
  `cd_BoardSize` matching its `bar[0]`; `AllocConfigDev` + `AddConfigDev` a
  synthetic board, `FindConfigDev` it, `RemConfigDev` + `FreeConfigDev`.
  (Population reads `g_pci_inv` directly, so no Process is needed — same
  caveat as the `nvme`/`pci` tests: needs the `-device` on the QEMU line,
  which the smoke harness provides.)

---

## 5. Testing

- **ConfigDev list + PCI population** (L14.1): the KERNEL_TEST above —
  list maintenance + the boot inventory surfaced as ConfigDevs.
- A `userexec`-style Gleas opening expansion.library + `FindConfigDev` via
  the proto stub proves the U-mode dispatch path (optional; the KERNEL_TEST
  covers the logic).

The slice ends on the standing gate: host `ctest` green, in-kernel runner
`0 failed`, format-check clean, two-boot QEMU smoke `ok`; commit; regen
`docs/LVO_COVERAGE.md`; handoff/memory follow-up.

---

## 6. Tracked gaps / deferrals

- **Zorro board config** (`ReadExpansionRom`/`WriteExpansionByte`/
  `ConfigBoard`/…) — no Zorro bus; the PCIe BAR allocator already
  configured every device at boot.
- **Expansion-memory allocation** (`Alloc/FreeBoardMem`, `Alloc/Free
  ExpansionMem`) — Zorro slot space; N/A.
- **The binding model** (`Obtain/ReleaseConfigBinding`, `Get/SetCurrent
  Binding`) — there is no driver-matching/auto-binding framework yet.
- **DOS boot nodes** (`MakeDosNode`/`AddBootNode`/`AddDosNode`) — CaraFS
  mounts over NVMe at boot (`docs/LOGAIC_BOOT.md`); a future mountlist /
  multi-volume boot story could revisit this, but it is not the v0 boot
  path.
- **16-bit `er_Product`** — the ABI struct's `er_Product` is a UBYTE, so a
  PCI device id is stored lossily (low byte). Matching by full 16-bit
  device id would need a CaraOS-private side field.
