# CaraOS Roadmap

Nine phases. Each has a single, falsifiable success criterion — when
the criterion is met, the phase is done. Work that doesn't advance a
phase's criterion is parked or deferred. Cross-phase scope creep is
the most likely way the project loses momentum, so we name it
explicitly here.

---

## Phase 0 — Foundation (in progress)

**Goal:** make subsequent phases possible.

**In scope:**

- `docs/ARCHITECTURE.md`, `docs/PRINCIPLES.md`, `docs/ROADMAP.md`,
  `docs/HARDWARE_RV2.md`, `docs/DTS_PARSER.md`.
- Hosted CMake build with clang, C23 enforcement, K&R `.clang-format`.
- C23 smoke test passes on hosted build.
- *(next)* `include/cara/list.h` + `include/cara/ring.h` with hosted
  unit tests.
- *(next)* `src/croi/fdt/` parser + captured DTBs in `tests/data/`.
- *(next)* RISC-V cross-build toolchain file, linker scripts for
  `splanc.efi` and `croi.elf`, ESP image builder.

**Done when:** Phase 1 work has somewhere to land.

This phase is not separately gated; we slide into Phase 1 once the
build targets a bootable artefact.

---

## Phase 1 — Boot to Clar

**Success criterion:**

> Splanc.efi boots from SD card on the OrangePi RV2 → Croi reaches
> multitasking → Clar (the Workbench analogue) draws on the framebuffer
> → a USB mouse and a USB keyboard plugged into the RV2 generate input
> events that move the pointer and type into Clar.

**Subgoals:**

1. **Boot.** `splanc.efi` on the FAT ESP partition. U-Boot launches it,
   it parses the FDT, hands off to Croi in S-mode.
2. **Croi runtime.** Trap vector, paging (Sv39), ASID allocation, frame
   allocator from FDT memory map, kernel heap, scheduler with at least
   two harts running, signals, MsgPort/Ring IPC, Handle table.
3. **Console.** Croi has a working kernel-internal `Croi_Print` against the X1's UART0
   discovered from the DTB. SBI early console up to that point.
4. **Framebuffer.** A simple framebuffer Clar can draw into. v0
   inherits whatever U-Boot set up (the FDT will expose it as a
   `simple-framebuffer` node when present); a from-scratch DPU /
   display-controller bring-up is **out of scope for Phase 1** and
   deferred to Phase 4.
5. **USB host.** xHCI driver on the X1's USB controller. HID class
   driver in user space (a Gleas) that posts events to Leargas.
6. **Leargas (Intuition).** Mouse pointer rendered, keyboard events
   delivered to the focused window.
7. **Clar (Workbench).** Background screen, at least one drawer
   (Bosca), can open and close it with the mouse, can type into a
   simple text Inntin (gadget).

**Out of scope for Phase 1:**

- Persistent storage of any kind. Boot is from a read-only mounted SD
  filesystem; there is no write-back.
- NVMe.
- GPU acceleration; rendering is CPU-side blits to the framebuffer.
- Audio, networking.
- Application porting beyond what Clar itself needs.

**QEMU equivalent:** the same Croi binary boots under
`qemu-system-riscv64 -machine virt -device qemu-xhci -device usb-kbd
-device usb-mouse` and shows Clar in the QEMU display output, with
the USB devices generating events through the same xHCI driver that
runs on the X1. No virtio shortcut — `qemu-xhci` is a real xHCI 1.0
implementation, so the daily driver and the silicon target run the
same code paths. Both must work; QEMU is the daily driver.

---

## Phase 2 — NVMe and a modernised FFS replacement

> **STATUS: criterion met under QEMU (2026-06-19); real-hardware
> sign-off pending the RV2 board.** All four subgoals are implemented:
> NVMe driver (N1–N5), CaraFS (F0–F5, on-disk format frozen at F4),
> the Logaic boot path (F6 G1–G4: GPT/UUID discovery, partition-relative
> mount, root selection, `S/Startup-Sequence`), and hosted mkfs/fsck.
> The two-boot QEMU smoke shows Clar editing a file in its drawer and
> the change surviving a reboot. A `STATUS: complete` line is reserved
> for the same demo on the physical RV2 (no board in hand yet). Designs:
> `docs/PHASE2_NVME.md`, `docs/CARAFS.md`, `docs/LOGAIC_BOOT.md`.

**Success criterion:**

> Croi mounts a CaraFS volume on an NVMe SSD attached via the M.2 slot
> of the OrangePi RV2; Clar boots from that volume, edits a file in a
> drawer, and the change persists across reboot.

**Subgoals:**

1. **NVMe driver.** PCIe enumeration on the X1, NVMe submission/completion
   queues, polled and IRQ-driven modes. Cleanroom from the NVMe and
   PCIe specs.
2. **CaraFS.** A modernised filesystem with the *flavour* of AmigaOS
   FFS but designed for modern storage:
   - 64-bit block addressing
   - directory hashing scaled for million-entry directories
   - journaling or copy-on-write for crash consistency
   - inode-equivalent with hard links
   - host-endianness clean (FFS was big-endian; we don't constrain)
   - extended attributes for the Amiga `.info` icon metadata pattern
   The full design is its own document — `docs/CARAFS.md` is created
   during this phase.
3. **Logaic boot path.** UUID/GPT-aware partition discovery, root
   volume mount, `S:Startup-Sequence` analogue executed at boot.
4. **mkfs / fsck.** Hosted tools (under `tools/carafs/`) that build
   and check CaraFS volumes from a host. No CaraFS code is needed in
   Splanc — Croi mounts post-handoff.

**Out of scope for Phase 2:**

- Network filesystems, NFS, SMB.
- RAID, LVM, encryption.
- Migration from FAT (the SD card path stays as a fallback boot route
  for development).

---

## Phase 3 — AmigaOS Release 2 (V36+) functional parity

> **Spec note.** The RKMs in `amigaos_kb_markdown/` are the **Third
> Edition** (©1992 Commodore-Electronics) — they target AmigaOS
> Release 2 / Kickstart 2.04, library versions V36+. They are a
> strict superset of 1.3 by every API CaraOS cares about, so
> "Release 2 parity" subsumes "1.3 parity." This is the actual
> charter; earlier docs in this project may say "1.3" loosely and
> should be read as Release 2.

**Success criterion:**

> Every library and standard tool documented in the 3rd Edition AmigaOS
> ROM Kernel Reference Manuals (the markdown set in
> `amigaos_kb_markdown/`) ships under its canonical V36+ name —
> `exec.library`, `dos.library`, `intuition.library`,
> `graphics.library`, etc. — with the canonical function names, struct
> shapes, and LVO offsets, behaviourally equivalent to the spec. **A
> well-written Release 2 application source listing builds against
> CaraOS headers and runs as a Gleas without source edits.** A
> representative sample of Release 2 applications (text editor, paint,
> file manager) is rebuilt from canonical-style source and validated
> against the spec'd test cases.

> **API-namespace rule.** The libraries below are produced under their
> verbatim AmigaOS V36+ filenames and ship the canonical function
> names, struct names, and LVO numbers (see PRINCIPLES.md §3.1 and
> ARCHITECTURE.md §7). The CaraOS-branded module name in parentheses
> is the *implementation team* — Croi/Logaic/Leargas/Dath — not the
> library a program calls. A program written for AmigaOS V36+ reaches
> these libraries by their AmigaOS names; the brand only appears in
> CaraOS source.

**Subgoals — 1.3 baseline (carried forward intact):**

1. **`exec.library`** (implemented by Croi): every documented V36+ Exec
   function — list/memory/task/signal/port/library/device primitives —
   exposed at its canonical LVO with its canonical signature.
2. **`dos.library`** (implemented by Logaic): locks, packets, console
   handler, FileLock semantics, the AmigaDOS process model.
3. **`intuition.library`** (implemented by Leargas): screens, windows,
   requesters, menus, gadgets, IDCMP message types.
4. **`graphics.library`** (implemented by Dath): RastPort drawing,
   fonts, blits, sprites, BitMap/View/ViewPort. The actual GPU binding
   is **Phase 4** work — Phase 3 produces the canonical API surface
   and a CPU rasteriser; Phase 4 wires the X1 GPU under it via the
   RTG-style driver model.
5. **Devices**: `console.device`, `input.device`, `serial.device`,
   `timer.device`, `keyboard.device`, `gameport.device` — each a
   `KOBJ_DEVICE` internally, exposed via `OpenDevice` /
   `CloseDevice` / `DoIO` / `SendIO` / `CheckIO` / `WaitIO` /
   `AbortIO`.

**Subgoals — V36 / Release 2 additions (the deltas the 3rd Edition adds):**

6. **`utility.library`**: tag-list helpers (`GetTagData`,
   `FindTagItem`, `NextTagItem`, etc.). Pervasive in Release 2
   APIs — implement first.
7. **BOOPSI** (Basic Object-Oriented Programming System for
   Intuition): class / instance / dispatcher protocol; the root,
   gadget, and image classes.
8. **`gadtools.library`**: V36 gadget toolkit — `NewGadget`, menu
   helpers, list/cycle/string gadgets.
9. **`asl.library`**: the file requester, font requester, and
   screen-mode requester. Tag-list-driven.
10. **`iffparse.library`**: IFF chunk traversal, push/pull parsing,
    property contexts. (IFF is the ubiquitous Amiga file-format
    family.)
11. **`commodities.library`**: input-event filtering and global hot
    keys.
12. **`icon.library`**: `.info` (Workbench icon metadata) parsing
    and writing.
13. **`diskfont.library`**: bitmap and outline fonts loaded from
    disk.
14. **`expansion.library`**: AutoConfig analogue — adapted to "what
    does the FDT parser see at boot?" rather than chained ROM probes.

**Subgoals — tools and porting:**

15. Standard tools (Gleasanna): `Ed`, `Format`, `Info`, `Status`,
    `List`, `Copy`, `Type`, `Search`, etc.
16. **Application source-build recipes**: a `docs/PORTING.md` that
    walks an author through building a 3rd Edition RKM application
    against CaraOS headers and demonstrates that the existing source —
    `OpenLibrary("intuition.library", 36)`, `AllocMem`, `AddTail`,
    `OpenScreen`, `OpenWindow`, IDCMP, …  — compiles and runs without
    textual changes. The "porting" effort is rebuild-and-link only;
    there is no name translation because there is no name divergence
    (PRINCIPLES.md §3.1).

**Out of scope for Phase 3:**

- 68k binary compatibility. We are RISC-V; original Amiga binaries
  don't run via this phase. The translator is Phase 9.
- Sound / `audio.library`. Audio device support arrives with
  Phase 5 SBC peripheral coverage.
- Networking. Phase 5 (driver) + Phase 7 (stack).
- `mathieee*` libraries beyond a thin shim that forwards to RV64's
  native IEEE 754 — no Motorola FFP / IEEE-translation surfaces
  needed.
- Outline-font rendering quality past what `diskfont.library`
  formally requires. Phase 7 will need better outline rendering for
  the web stack and that work lives there.

---

## Phase 4 — GPU (RTG-flavoured driver model + the X1 GPU)

**Depends on:** Phase 3's `graphics.library` (the verbatim AmigaOS V36+
graphics API surface, implemented internally by the **Dath** module).
Phase 4 does **not** invent a new graphics API; it provides an
RTG-style driver that sits underneath `graphics.library` and binds the
API to the X1 GPU.

**Success criterion:**

> A cleanroom CaraOS GPU driver runs 2D and 3D acceleration through
> `intuition.library` on the X1's GPU. The driver registers with
> `graphics.library` via the RTG vector-table model adapted from the
> 1993 Developer Conference "Retargetable Graphics Specification": it
> provides driver entry points (`drv_LoadView` / `drv_MakeVPort` /
> `drv_VideoControl` / etc.), populates the graphics database with
> ModeIDs the X1 actually supports, and exposes a true-colour pixel
> type. The only third-party binary in the CaraOS image is the GPU's
> microcode firmware blob (the documented "necessary blob" exception
> in `docs/PRINCIPLES.md`). 2D acceleration measurably outperforms
> the Phase 1 CPU blitter on the same workload; 3D runs a non-trivial
> demo. The composited Clar desktop sustains **1920 × 1080 at 60 Hz
> with triple-buffered presentation** through `intuition.library` on
> the RV2 — see `docs/PRINCIPLES.md` §4.1, the project-level
> performance budget this phase has to satisfy.

### RTG scoping (from the 1993 Conference RTG Specification)

The 1993 Developer Conference notes
(`amigaos_kb_markdown/International_Amiga_Developers_Conference_Notes_1993_Commodore.md`,
section *Retargetable Graphics Specification*) propose an architecture
where graphics drivers are vectored *into* `graphics.library` rather
than running as standalone libraries. This is exactly the right model
for the X1 — one device, no chip set — and Phase 4 implements a
**CaraOS-native subset** of it.

**What CaraOS adopts from the RTG spec:**

1. **Vectored driver model.** The X1 GPU driver is a set of routines
   hooked into `graphics.library`'s entry points. The driver-base
   pointer travels in a stable kernel-side register; the
   `graphics.library` base — i.e. classic AmigaOS `GfxBase`, which
   programs hold in `A6`-equivalent — is supplied by the runtime.
2. **Graphics database.** ModeIDs identify (resolution × pixel-type
   × refresh) tuples. The canonical `BestModeID()` and
   `NextDisplayInfo()` calls from V36+ `graphics.library` query and
   iterate it; a CaraOS-extension `AddDisplayMode(...)` (LVO past the
   classic range, see ARCHITECTURE.md §7.2) lets the driver populate
   the database from the driver-init path.
3. **PixelType IDs.** RTG's 4-character pixel-type tags (`HAM`,
   `EHB`, `TRUE`, `PLUT`, `PRGB`). For Phase 4 we register **`TRUE`**
   (32-bit RGBA true-colour) as the X1's only pixel type. `HAM` /
   `EHB` / `PLUT` exist in the database enums for source
   compatibility but never appear on the X1 mode list.
4. **Friend-bitmap pattern.** RTG's "friend bitmap" idiom — passing
   a skeleton bitmap to `AllocBitMap` so the allocator picks
   device-compatible storage. The canonical `AllocBitMap(width,
   height, depth, flags, friend)` from V36+ `graphics.library`
   consults the friend's ModeID to choose the X1 GPU's tiling and
   stride.
5. **Driver entry-point subset** sufficient for Phase 4:
   `drv_LoadView`, `drv_UnloadView`, `drv_MakeVPort`, `drv_MakeView`,
   `drv_ObtainDBufInfo`, `drv_ReleaseDBufInfo`, `drv_VideoControl`
   (tag-list-driven config: DPMS, gamma, etc.), `drv_WaitTOF`,
   `drv_WaitBeam`, `drv_RefreshColors` (no-op for true-colour but
   kept for future PLUT support).

**What CaraOS does *not* adopt from the RTG spec:**

- **HAM / EHB / PLUT pixel-type implementations.** The X1 is
  true-colour hardware; legacy palette / packed modes are not Phase 4
  scope. (A future "AGA emulation" Gleas could provide them in
  software for retro apps.)
- **`MonitorSpec` scan-rate-subcode bit packing.** RTG packed
  device-class / scan-rate / unit into a 16-bit ModeID for the
  classic chip-set generations. The X1 has one display engine; our
  ModeID space is laid out CaraOS-natively (the implementation lives
  under the brand-namespace **Dath** module). Programs see canonical
  V36+ ModeID values from `<graphics/displayinfo.h>`; the bit-pack
  layout under those values differs from classic AGA, which is
  invisible to user code that uses `BestModeID`/`NextDisplayInfo`
  rather than constructing ModeIDs by hand. Format documented in
  `src/dath/modeid.h` (TBD; brand-namespace internal).
- **Conditional ECS / AA / AAA codepaths.** Irrelevant on RV2.
- **Multi-driver coexistence.** RTG envisaged native chip set + one
  or more third-party graphics boards alive simultaneously. Phase 4
  ships with one driver (X1 GPU). The model still supports a second
  driver later, but we don't exercise that path.

### Subgoals

1. **Identify X1 GPU IP and its register documentation.** The X1
   DTS exposes `dpu_reserved` for the DPU; the 3D GPU is separate
   and needs locating in the SoC reference manual (TBD task).
2. **Cleanroom kernel-mode-setting driver.** Mode set, framebuffer
   ownership, page-flipping. Hooked into `graphics.library` via the
   RTG-style vector table above.
3. **Command-stream submission.** Ring buffer of GPU commands,
   fences, exception handling.
4. **The microcode blob.** Tracked in `firmware/x1-gpu/` with
   provenance and SHA-256 in the directory README. Loaded at GPU
   init only; never modified at runtime.
5. **`graphics.library` RTG glue.** Implements canonical
   `AddDisplayMode` / `BestModeID` / friend-aware `AllocBitMap` and
   the RTG-style vector-table dispatch pattern. Implementation lives
   in `src/dath/`.
6. **`intuition.library` integration.** Hardware-accelerated RastPort
   ops where the GPU can do them, CPU fallback otherwise. The Phase 1
   CPU blitter remains as the fallback path.

### Out of scope for Phase 4

- Compute / OpenCL / CUDA-style.
- Vulkan / OpenGL conformance. We expose a CaraOS-native API.
- Multi-display.
- HAM / EHB / PLUT pixel-type rendering.
- Hot-pluggable third-party graphics boards.

---

## Phase 5 — SBC peripheral coverage

**Success criterion:**

> "The most useful hardware on the OrangePi RV2 has drivers." A hobby
> developer / gamer can boot CaraOS on a stock RV2 and have audio,
> Ethernet, HDMI output, common HID classes (gamepads, mice,
> keyboards), and general I2C/SPI/GPIO access without external help.

**Subgoals (each independently scoped):**

1. **Audio.** The X1 audio codec; a `ceol.device` (Irish for "music")
   exposing PCM playback and capture.
2. **Ethernet.** The X1's MAC; a `lion.device` (Irish for "line")
   exposing raw frames; a TCP/IP stack later (may push into a Phase 6).
3. **HDMI.** Output via the DPU once Phase 4 has the driver.
4. **HID classes.** Beyond mouse/keyboard from Phase 1: gamepads
   (Xbox/Steam-style + generic), USB-C audio devices.
5. **General-purpose I/O.** A user-space-facing I2C / SPI / GPIO API
   surfaced through Logaic device handlers.
6. **Wi-Fi.** Will require a firmware blob, like the GPU. *Decision
   pending* on whether to take a second blob exception in Phase 5.
   Documented as an explicit phase-5 question rather than assumed.

**Out of scope for Phase 5:**

- High-end peripherals not present on the RV2 SKU (e.g., M.2 NPU
  modules).
- Cellular modems.
- Industrial fieldbus.

---

## Phase 6 — On-target C23 toolchain (the Cara-Lattice experience)

**Success criterion:**

> Sitting at a running Clar desktop on real RV2 hardware, a CaraOS user
> can edit a `.c` source file, compile it with the on-target compiler,
> link the resulting object with the on-target linker, and run the
> output as a Gleas. The compiler accepts a documented subset of C23
> and emits correct RV64GCV machine code (correctness, not speed). On a
> small program, the edit-compile-run round-trip is measured in seconds
> on RV2 silicon.

**The "modern Lattice-C" goal.** Lattice-C in the classic AmigaOS era
was the practical C compiler for Amiga developers: tightly integrated with
the OS, fast on the metal of the day, familiar workflow. Phase 6 aims
for that same *feel* on modern RISC-V silicon — snappy, native, and
integrated with Clar / Guth / Logaic.

This is **explicitly not** a clang competitor and **explicitly not**
parity with the host build. For production builds and aggressive
optimisation, use the host toolchain (which already cross-targets RV64
via the `riscv64-unknown-elf` clang triple). The on-target toolchain
exists so that someone running CaraOS can write CaraOS code on the
machine they booted into, not so that we can stop using clang.

**Tool names** (proposed, following the Irish-derived nomenclature from
ARCHITECTURE.md §13). These are placeholders subject to confirmation:

| Tool      | Role          | Irish meaning      | AmigaOS analogue     |
|-----------|---------------|--------------------|----------------------|
| `cas`     | assembler     | "twist / turn"     | `omd` (Lattice asm)  |
| `tion`    | C compiler    | from *tiomsú*, "compilation" | `lc1` / `lc2`     |
| `nasc`    | linker        | "link, connect"    | `blink`              |
| `taisce`  | archiver      | "store, treasury"  | `oml`                |
| `dean`    | build driver  | "do, make"         | `make`               |

**Subgoals:**

1. **`cas`** — RV64GCV assembler. Subset to start: `rv64ima` plus the
   CSR and `sfence.vma`/`sfence.w.inval` instruction families needed
   for system code. Compressed (C) and vector (V) instruction encodings
   added incrementally. Emits ELF relocatable objects.
2. **`tion`** — C23 compiler core. **Non-optimising.** Two-pass: parse
   → AST → typecheck → straightforward stack-allocator codegen → emit
   `cas` text. The C23 subset is documented as it solidifies; the v0
   target:
   - all C99 baseline plus C11 atomics (we use them ourselves)
   - C23 essentials: `nullptr`, `constexpr` (constant expressions
     only — not the function-decoration usage), `typeof`,
     `[[nodiscard]]`, designated initializers, `enum : type`
     underlying type
   - explicitly **out** for v0: `_BitInt`, generic `auto`, `#embed`,
     `#elifdef`/`#elifndef`. They land later as the parser grows.
3. **`tion`'s codegen quality.** Not optimised. No inlining, no DCE,
   no register coalescing, no loop unrolling, no constant folding
   beyond what falls out of the AST naturally. Each automatic variable
   gets a stack slot; computation happens through a small fixed
   register set. This is documented as a Phase 6 *feature*, not a
   bug.
4. **`tion` driver.** A `cc`-style entry point that wires preprocessor
   → `tion` → `cas` → `nasc` and produces a runnable Gleas from a
   single `.c` file. Mimics the gcc/cc invocation surface enough that
   small Makefile-style snippets work out of the box.
5. **`nasc`** — linker. ELF relocatable input, CaraOS-native executable
   output. Resolves symbols, applies the RV64 relocations actually
   emitted by `cas`, lays out segments. Static linking only at this
   phase; shared-library / dynamic linking is deferred.
6. **`taisce`** — archiver. `.a`-style object archives so a small
   library author can ship one file. BSD-`ar`-flavoured format.
7. **`dean`** — build automation. A minimal `make`-flavoured tool.
   Recipe-and-prerequisite model. No fancy variables, no implicit
   rules beyond `.c → .o → .gleas`. Big enough to drive a hobby
   project; small enough to read in a single sitting.
8. **On-target `libcara`.** The userspace runtime stub already shipped
   with Phase 1 grows the C23 stdlib subset that `tion` emits calls
   to: `<string.h>`, `<stdio.h>`, `<stdlib.h>` (subset),
   `<stdatomic.h>`, plus the `cara/*` headers. Same headers, same
   shapes as the host build; programs that compile with `tion` on
   CaraOS also compile with host clang against the same headers.

**Out of scope for Phase 6:**

- **Self-hosting CaraOS.** Phase 6 does not aim to compile CaraOS
  itself; the host toolchain (clang + CMake) remains how CaraOS is
  built. *Self-hosting `tion`* (compiling Phase 6 with itself on RV2)
  is a stretch goal, not the success criterion.
- **Optimisation.** Stays out. People who need it use the host.
- **C++.** We are a C OS in C compiler.
- **Position-independent executables**, dynamic linking, RTLD-style
  runtime symbol resolution. Static-only at first.
- **IDE / debugger / profiler.** Editor is the Phase 3 `Ed` analogue.
  Debugger is its own future phase.
- **Cross-compilation from the host.** `tion` runs on CaraOS, full
  stop. To cross-build for CaraOS from a host machine, use clang.

---

## Phase 7 — Basic web experience (HTML5 / CSS, HTTP/HTTPS, no JS)

**Depends on:** Phase 5 (Ethernet driver). Phase 4 (GPU) optional —
nice for fast scrolling, not required.

**Success criterion:**

> A CaraOS user runs a Cara-native browser as a Gleas, types a URL,
> and reads a JavaScript-free HTML5 page styled with its own CSS over
> HTTPS. Wikipedia article pages, plain documentation sites, and raw
> markdown rendering on common forges are usable enough to read.

**The "no JavaScript" constraint** is the load-bearing scope
simplifier. It removes the JS engine, the post-load DOM mutation
problem, and the WebAssembly question entirely. What remains is the
read-the-spec core: parse, layout, paint.

**Subgoals:**

1. **TCP/IP stack.** From-scratch IPv4/IPv6 on the Phase 5 Ethernet
   MAC. ICMP, UDP (for DNS), TCP. RFCs 791 / 792 / 793 / 8200 etc.
   are the source.
2. **DNS resolver.** RFC 1035 — A and AAAA — with a system cache.
3. **TLS 1.3 client.** RFC 8446. The largest single piece of work
   in Phase 7. From scratch is non-trivial: ECDHE (RFC 7748 X25519,
   FIPS 186-5 P-256), AES-GCM (NIST SP 800-38D), ChaCha20-Poly1305
   (RFC 8439), HKDF (RFC 5869), SHA-2 (FIPS 180-4) — each from its
   own spec. **No** OpenSSL / BoringSSL / mbedtls. **No** TLS 1.2 or
   earlier.
4. **HTTP/1.1 client.** RFCs 7230–7235. HTTP/2 deferred.
5. **HTML5 parser.** WHATWG HTML Living Standard: tokeniser plus
   the tree-construction state machine. Output is a DOM-equivalent
   tree.
6. **CSS parser + cascade.** CSS Syntax Level 3 for parsing; CSS 2.1
   plus selected Level 3 modules (Box, Flexbox, Fonts, Backgrounds)
   for the cascade. Grid is a stretch goal.
7. **Layout engine.** Block / inline / replaced / flex; line breaking
   per UAX#14.
8. **Painter.** Maps the laid-out box tree to Leargas RastPort calls;
   uses Phase 4 GPU when present.
9. **Text rendering.** TrueType outline rendering from the OpenType /
   TrueType specs — no FreeType. Subpixel positioning, basic hinting,
   Latin / Greek / Cyrillic shaping. Complex shaping (Arabic, Indic)
   is a stretch.
10. **Image decoders.** PNG (RFC 2083), JPEG baseline (ISO/IEC
    10918-1), GIF89a — from spec, no libpng / libjpeg / giflib.
    WebP and AVIF deferred.
11. **The browser Gleas itself.** Address bar, history, back / forward,
    one-tab UX. Multi-tab is a stretch; "view source" is recommended.

**Cara name (TBD):** placeholder `Léarscáil` ("map / chart") for the
browser.

**Out of scope for Phase 7:**

- **JavaScript** — explicitly excluded; the rest of the phase
  collapses if this slips back in.
- WebAssembly.
- HTTP/2, HTTP/3, QUIC.
- TLS 1.2 and earlier.
- WebRTC, WebSockets, ServiceWorker, streamed `Fetch`.
- DRM-protected media (EME).
- Forms beyond `<form>` GET/POST. No client-side validation.
- Cookies beyond session-scoped `Set-Cookie` storage.

**Why this is achievable without third-party deps.** HTML, CSS, HTTP,
TLS, PNG, JPEG, GIF, OpenType, and the relevant Unicode algorithms
are all public, stable specs. The work is enormous but bounded.
Implementing a JavaScript engine is unbounded — that's why it is
excluded.

---

## Phase 8 — Deluxe Paint for 2026

**Depends on:** Phase 4 (GPU acceleration) and Phase 5 (tablet /
gamepad input). Runs on a CPU painter without Phase 4, slowly.

**Success criterion:**

> A flagship cleanroom paint application runs as a Gleas on CaraOS,
> drawing on a 2026-relevant canvas (high-DPI, RGBA, large-format)
> with the workflow ergonomics of the original Deluxe Paint —
> palette / brush / pattern / animation idioms — extended with modern
> conveniences (layers, deep undo, modern colour pickers). Tablet /
> stylus pressure is supported; gamepad input maps to brush controls.

**The "for 2026" framing.** Respect what made DPaint great: snappy,
keyboard-driven, brush-as-tool ethos. Update what reads as quaint
today: 256-colour palettes as the *only* mode, postage-stamp canvases,
single-image workflow. It is a re-imagining, not a clone.

**Subgoals:**

1. **Canvas model.** RGBA8 and RGBA16; layered. Target ceiling:
   16k × 16k, 16 layers. Indexed-palette mode preserved as a
   first-class working mode for retro / pixel-art workflows.
2. **Brush engine.** Image-as-brush carry-forward (an AnimBrush
   concept), dab spacing, jitter, scattering. Shape brushes,
   custom brushes, palette brushes.
3. **Tools.** Pencil, brush, fill, gradient, line, rectangle,
   ellipse, polygon, freehand, marquee, lasso, magic wand. Each
   tool one-key shortcut, DPaint-style.
4. **Animation.** AnimPaint-style frame stack, onion-skinning,
   per-frame timing, looped preview. Output as APNG and as a
   CaraOS-native animation file (TBD format) recorded in CaraFS
   extended attributes.
5. **Layers and undo.** Many layers (vs DPaint's single canvas);
   non-destructive editing; deep undo. The most important *modern*
   concession.
6. **Colour.** Modern pickers (HSL, OKLab, palette swatches);
   backwards-compatible 256-colour palette mode.
7. **Input.** Tablet / stylus pressure via USB HID (Wacom AES-class
   protocols from spec, no `libwacom`). Gamepad mapping.
   Keyboard-first interaction where it makes sense.
8. **GPU acceleration.** Brush dabs and layer composites run on the
   Phase 4 GPU when present; CPU rasteriser otherwise.

**Cara name (TBD):** `Ealaín` ("art") or `Bua` ("victory / triumph")
— flagship gets a flagship name; placeholder until confirmed.

**Out of scope for Phase 8:**

- Vector / SVG editing. Different application.
- Photo retouching. Different application; could be a sibling.
- 3D painting / texture authoring.
- Print colour management, ICC profiles.
- Plugin architecture. Monolithic at first.

**Why this is the right flagship.** Deluxe Paint defined the Amiga's
public face. A paint application as the CaraOS flagship honours the
lineage and gives the project a recognisable artefact.

---

## Phase 9 — 68k → RV64 binary translator

**Depends on:** Phase 3 (the AmigaOS library surface that translated
binaries call into). May force some additional 2.x library work
*inside* this phase — see "Acknowledged scope expansion" below.

**Success criterion:**

> A pre-existing AmigaOS 1.3 or 2.x 68k binary, distributed in the
> classic Hunk executable format, runs on CaraOS via a binary
> translator that converts 68k machine code to RV64 and remaps
> library calls to their CaraOS analogues. Programs that use
> AmigaOS APIs *correctly* run; programs that bypass the OS to "bang
> the hardware" are detected at load and rejected with a clear
> diagnostic.

**Architecture sketch — Rosetta-ish:**

1. **Hunk loader.** Amiga executable format (HUNK_HEADER, HUNK_CODE,
   HUNK_DATA, HUNK_BSS, HUNK_RELOC32, HUNK_END, …) is fully
   specified in the AmigaDOS Manual / RKM Includes & Autodocs already
   sitting in `amigaos_kb_markdown/`. Loader maps each hunk into a
   SASOS region and applies relocations.
2. **68k decoder + translator.** Per-basic-block 68000 / 68020
   translation into RV64, cached in a translation buffer. AOT
   translation of statically reachable code at load time; JIT for
   newly discovered branch targets. Self-modifying code handled via
   MMU dirty-page tracking and translation invalidation.
3. **CPU state model.** D0–D7, A0–A7, PC, CCR, USP/SSP, FPU regs (if
   the binary uses 6888x). Mapped onto a fixed RV64 register tile
   plus a spill area; Motorola flag semantics preserved.
4. **Memory model.** The 68k flat address space is mapped into a
   dedicated SASOS region at the same virtual addresses the binary
   expects. 68k pointers stay valid without rewriting — exactly what
   SASOS buys us.
5. **Library call remapping.** When 68k code executes the canonical
   `JSR -nnn(A6)` library-call form, the translator recognises the
   LVO and emits a direct RV64 call to the corresponding CaraOS
   Phase 3 entry point — `AllocMem` (exec.library), `Open` (dos.library),
   etc. The LVO
   map of every CaraOS library is declarative input to the
   translator. **This is the technically interesting content of
   Phase 9.**
6. **Trap-on-banger.** Direct hits to legacy custom-chip space
   (CIA at `$BFE001`/`$BFD000`, custom chips at `$DFF000`–`$DFF1FE`)
   are not mapped. A 68k binary that touches them faults; the
   translator reports "hardware-banger; not supported" and
   terminates.

**The "no hardware bangers" rule, in detail:**

- **No custom-chip emulation.** No COPPER, no BLITTER, no Paula
  audio, no Agnus, no Denise. Programs that drive these directly
  do not run.
- **No 68k FPU emulation beyond what `imafd` provides** for the
  standard subset. The 68881/68882 transcendentals (`FETOX`,
  `FCOSH`, etc.) are best-effort; programs that hard-depend on
  bit-exact FPU semantics may be rejected.
- **No DMA, no interrupt-vector-installed driver code.** Programs
  that install custom interrupt handlers in the 68k vector table
  are out. Programs that use `exec.library` `AddIntServer` are in,
  remapped to Croi.
- **No 1.3 boot-block / disk-format games.** These weren't OS
  programs in the first place.
- **No protected / encrypted "Copylock"-style executables.** The
  Hunk loader rejects them at load.

**Phase 3 dependency.** Phase 3 already delivers AmigaOS Release 2
parity (the 3rd Edition RKMs *are* the 2.x reference). `asl.library`,
`gadtools.library`, `commodities.library`, BOOPSI, and the widened
`dos.library` are therefore present before Phase 9 starts. Phase 9
just remaps `JSR -nnn(A6)` LVOs onto the existing CaraOS entry
points. There is no 2.x-shim work hidden inside Phase 9.

**Cara name (TBD):** `Aistreoir` ("translator") is the natural pick.
The translator runs as a Croi-linked component rather than a user
Gleas, since it must install MMU fault handlers for its
self-modifying-code detection.

**Out of scope for Phase 9:**

- 68040 / 68060-only software (dependent on those CPUs' MMUs and
  FPU edge cases).
- AmigaOS 3.x-only programs (different library surface; if pursued,
  a Phase 9.x extension).
- AmigaOS 4.x / MorphOS / AROS-only PowerPC binaries — different
  architecture entirely.
- AGA-aware bangers (a strict subset of "no hardware bangers").
- "Patch-in-place" toolkits that overwrite OS library vectors — they
  rely on knowing the exact 68k jump-table layout, which we don't
  preserve.

---

## Tracking phase progress

Phase status lives in this file. When a phase completes its criterion
on real hardware, this file gets a `**STATUS: complete (yyyy-mm-dd)**`
line. We do not track sub-task status here; that belongs in code
comments, commit messages, or a separate tracker. This document
captures the *plan*, not the day-to-day.
