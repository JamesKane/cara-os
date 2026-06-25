# CaraOS engineering principles

These are the rules we agree to follow on every change. They sit *above*
the architecture: an architectural decision can change without rewriting
this document; a violation of one of these principles needs an explicit,
documented exception.

There are six principles. The first four are policy. The last two are
development style.

---

## 1. License: BSD-2-Clause, SPDX in every source file

CaraOS is BSD-2-Clause. The full text is in `LICENSE` at the project
root. Every source file CaraOS owns starts with an SPDX identifier:

```c
// SPDX-License-Identifier: BSD-2-Clause     <-- C, headers
```

```cmake
# SPDX-License-Identifier: BSD-2-Clause      <-- CMake, .clang-format,
                                                 .editorconfig, shell
```

Files exempt from the header:

- `LICENSE` itself
- `.gitignore`
- Markdown documentation (currently)

Why: a single permissive license simplifies redistribution; per-file SPDX
is the SPDX-2.x convention used by the Linux kernel and most modern BSD
projects, and it makes license auditing automatable. Auditors should be
able to grep `SPDX-License-Identifier:` and see exactly one license
across the tree.

How to apply: if you create a new file, add the line as line 1. If you
edit a file that's missing it, add it as part of your edit. CI is
expected to enforce this once it exists; for now the convention is
honour-system.

---

## 2. No third-party dependencies in CaraOS-linked code

CaraOS-linked code is written from scratch by deriving from primary
sources: published specs, RFCs, vendor datasheets, and read-only
inspection of reference headers and implementations. We do not vendor or
link any third-party library into the CaraOS image.

This includes attractive ones:

- `libfdt` — read it as a reference; do not link it.
- FAT / exFAT reference implementations — read the FAT specification;
  write our own.
- USB stacks (TinyUSB, libusb-style) — read the USB specifications and
  the xHCI specification; write our own.
- NVMe drivers — NVMe spec is public; write our own.
- Font libraries (FreeType) — TrueType / OpenType specs are public.
- Compression libraries — write the few we need from the format spec.

What is *not* a "third-party dependency" in this sense:

- **Tools** that produce CaraOS binaries: `clang`, `cmake`, `dtc`,
  `qemu`. They are build/test infrastructure, not linked code.
- **Firmware that ships outside the CaraOS image**: OpenSBI in M-mode,
  U-Boot, and the platform-supplied DTB. We consume their interfaces
  (SBI, UEFI, FDT) — those are specifications, not vendored code.
- **Reading** a third-party implementation as a cross-check during
  testing. The DTS parser test plan, for example, runs `libfdt` over
  the same blob and asserts our parser agrees. `libfdt` is not in the
  CaraOS binary; it lives in the test harness only.

### The Phase 4 GPU exception

Modern GPUs require a microcode firmware blob loaded into the GPU at
init. The X1's GPU is no different. Phase 4 (GPU support) tolerates
**one** such blob: the necessary microcode for the X1's GPU. The driver
itself is cleanroom CaraOS code derived from published register
documentation and the GPU's open-source kernel-mode-driver source as a
reference. The blob is treated as platform firmware, like the DTB or
OpenSBI: not part of CaraOS, but loaded by it.

This is the **only** documented exception to the no-deps rule. Wi-Fi
firmware, Bluetooth firmware, and any other "the silicon won't run
without this opaque binary" cases are Phase 5 questions that will need
their own carve-outs at that time, with the same standard:
microcode-only, driver written from scratch.

### Why we hold this line

- One license: makes redistribution unambiguous.
- No trademark / IP entanglement: avoids the ROM-IP minefield that has
  haunted Amiga successors for thirty years.
- The cleanroom mission: pulling in a third-party FFS or Workbench
  would defeat the point.
- It's fun. The hobby of CaraOS is *implementing* these things.

---

## 3. Cleanroom from primary sources

CaraOS is implemented by reading the spec and writing fresh code. This
applies at three levels:

1. **AmigaOS surface (Release 2, V36+).** The 3rd Edition RKMs in
   `amigaos_kb_markdown/` are the functional specification — they
   cover Release 2 (Kickstart 2.04), which is a strict superset of
   1.3 by every API CaraOS targets. We never copy code or text from
   the original sources into CaraOS, but we are bound to the spec's
   *names and shapes* — see §3.1 below.
2. **Hardware.** The Spacemit/Ky X1 hardware addresses come from the
   linux-orangepi vendor DTS. We *parse* the DTB at boot rather than
   transcribing constants — see ARCHITECTURE.md §9 and DTS_PARSER.md.
3. **Standards.** When we implement FAT, USB, NVMe, ATA, TCP/IP, or any
   other protocol: read the standard, write our own.

Read references, but write fresh. If a reference's code structure
demonstrably leaks into ours, that's a bug.

### 3.1 Brand namespace vs API namespace

CaraOS keeps two disjoint namespaces and lets neither bleed into the
other. The contract is:

> **A well-written AmigaOS V36+ source program compiles for CaraOS.**

That contract is the load-bearing reason this rule exists. If a program
written against the 3rd Edition RKMs needs identifiers renamed before
it builds for CaraOS, the project has failed at its single biggest
source-compatibility promise. Source compatibility is *not* binary
compatibility — Phase 9 covers the 68k → RV64 translator; Phase 3
covers the source-level surface. The rule below is what makes Phase 3
buildable from existing AmigaOS source listings without textual edits.

**Brand namespace** — applies to everything *under* the API. Identifiers
the user's program never sees:

- The project: **CaraOS**, **Cara**.
- Kernel binary: **`croi.elf`**. Boot loader: **`splanc.efi`**.
- Source tree directories: `src/croi/`, `src/leargas/`, `src/logaic/`,
  `src/clar/`, `src/guth/`, `src/dath/`, …
- Internal kernel-only C symbols: `Croi_TrapDispatch`, `Croi_Time_Now`,
  `Croi_Page_Alloc`, `Dath_Framebuffer_FromFdt`, etc. — anything that
  exists *only* inside the kernel, never linked by user programs.
- Marketing, documentation prose, glossary terms.

The brand namespace draws from Irish-derived nomenclature — see the
table in ARCHITECTURE.md §13 — and is where CaraOS's identity lives.

**API namespace** — applies to everything a user program references.
This namespace is **verbatim AmigaOS V36+** as documented in
`amigaos_kb_markdown/`. No renames, no prefixes, no decorations.
Specifically:

- **Library filenames** in the resident library region (lower-half VA
  `0x4000_0000`+, see ARCHITECTURE.md §4.3): `exec.library`,
  `dos.library`, `intuition.library`, `graphics.library`,
  `utility.library`, `gadtools.library`, `asl.library`,
  `iffparse.library`, `commodities.library`, `icon.library`,
  `diskfont.library`, `expansion.library`, `keymap.library`,
  `layers.library`, `workbench.library`, `mathieeesingbas.library`,
  `mathieeedoubbas.library`, `mathieeesingtrans.library`,
  `mathieeedoubtrans.library`, `translator.library`, `version.library`.
- **Device filenames**: `audio.device`, `console.device`,
  `gameport.device`, `input.device`, `keyboard.device`,
  `narrator.device`, `parallel.device`, `printer.device`,
  `ramdrive.device`, `scsi.device`, `serial.device`, `timer.device`,
  `trackdisk.device`.
- **Public C symbols**: `OpenLibrary`, `CloseLibrary`, `AllocMem`,
  `FreeMem`, `AddTail`, `AddHead`, `RemHead`, `RemTail`, `Remove`,
  `NewList`, `PutMsg`, `GetMsg`, `WaitPort`, `Wait`, `Signal`,
  `AllocSignal`, `FreeSignal`, `SetTaskPri`, `OpenDevice`, `DoIO`,
  `Move`, `Draw`, `RectFill`, `OpenScreen`, `OpenWindow`,
  `GetTagData`, `FindTagItem`, `NextTagItem`, … — every spelling as
  the autodocs print it.
- **Public C struct names**: `Library`, `Task`, `MsgPort`, `Message`,
  `Node`, `MinNode`, `List`, `MinList`, `IORequest`, `Device`,
  `RastPort`, `BitMap`, `Window`, `Screen`, `Gadget`, `IntuiMessage`,
  `TagItem`, … — verbatim, including field-name prefixes
  (`ln_Succ`, `mp_MsgList`, `tc_Node`, etc.).
- **LVO numbers**: every library exports its functions at the LVO
  offsets the V36+ autodocs document. The four per-library reserved
  slots (LIB_OPEN, LIB_CLOSE, LIB_EXPUNGE, LIB_EXTFUNC) are at -6,
  -12, -18, -24; user-defined functions start at -30 (LIB_USERDEF).
  Every per-library LVO matches the canonical V36+ value.
- **Tag IDs**, **IDCMP class flags**, **IFF chunk type codes**,
  **error code numerics** — all as documented.

The namespaces meet only at the trampoline: an `exec.library` LVO
entry (e.g. `_LVOAllocMem` at `LIB_BASE - 30 - n*6`) jumps to a
brand-namespace implementation function (e.g. internal
`Croi_AllocMem_Impl`). Programs never see the brand-side symbol;
implementations never expose the LVO trampoline as their canonical
name.

**Where the line falls in headers.** The kernel's internal headers
(`include/cara/*.h`) belong to the brand namespace and may use `Croi_`,
`Dath_`, etc. The Phase 3+ public AmigaOS-shaped headers
(`include/exec/*.h`, `include/dos/*.h`, `include/intuition/*.h`,
`include/graphics/*.h`, …) belong to the API namespace and use AmigaOS
names verbatim — the same `<exec/lists.h>` / `<exec/memory.h>` /
`<intuition/intuition.h>` paths a 1992 program `#include`d.

**How to apply.**

- When you write a function that user programs may call, the C
  symbol's name is whatever the AmigaOS autodocs print. No discretion.
- When you write a struct that user programs may reference (by name,
  by `sizeof`, or by field), the name and field names are whatever
  the AmigaOS includes show. No discretion.
- When you write something user programs cannot see (kernel scheduler,
  page tables, the FDT parser, the GPU driver-side glue under
  graphics.library), name it from the brand namespace.
- If you find yourself wanting a `Croi_AllocMem` or a `Cara_OpenLibrary`
  in a public header, you've crossed the line — drop the prefix.
- If you find an AmigaOS name (`Task`, `Message`, `OpenLibrary`) used
  for something internal that has no AmigaOS analogue, rename to the
  brand namespace — squatting on the name closes off the trampoline.

---

## 4. Performance and resource budgets

CaraOS commits to two quantitative budgets up front. They are not
aspirations; they are the line below which the project has failed.
Every change is measured against them, and a regression on either
blocks a merge the same way a correctness regression does.

### 4.1 1080p triple-buffered at 60 Hz, sustained

The composited Clar desktop must run **1920×1080 at 60 Hz with
triple-buffered presentation** on the OrangePi RV2's GPU, sustained,
without frame drops or tearing during typical interactive use.

If we can't, we have failed.

Why this is the line:

- 1920 × 1080 × 4 bytes × 3 surfaces ≈ 24 MiB of framebuffer; the
  frame budget is 16.67 ms. The X1 GPU has the bandwidth and shader
  throughput to do this comfortably. Missing it means we have left
  performance on the floor in the driver, the compositor, or both —
  not that the silicon can't keep up.
- 60 Hz with three surfaces (front + back + working) eliminates
  tearing without the input-lag penalty of strict double-buffered
  vsync. Below this, the desktop *feels* worse than the 1992
  baseline, which makes the entire project pointless.
- The constraint forces the GPU driver (Phase 4 — internally branded
  the **Dath** driver), the Leargas compositor under `intuition.library`,
  and the `graphics.library` flip path to be measured rather than
  hand-waved. It also forecloses lazy designs (single framebuffer,
  blit-on-vblank) that would compromise responsiveness.

How to apply: every change touching the display path is benchmarked
against this. The Phase 4 success criterion in `docs/ROADMAP.md`
states it explicitly and is verified end-to-end on RV2 silicon.

### 4.2 128 MiB ceiling for kernel + system services

Croi plus all in-kernel and resident system services — libraries,
device drivers, daemons — must fit within **128 MiB of resident
physical memory** under typical interactive load.

If we exceed it, we have failed.

The implication is that **CaraOS does not page anonymous memory to
disk.** No swap, no page-out daemon, no working-set tracking. RAM is
RAM; if a workload doesn't fit, it doesn't fit, and the user gets a
clear out-of-memory diagnostic rather than latency cliffs and disk
thrashing.

Why this is the line:

- A base 2 GiB OrangePi RV2 is the cheapest tier and the assumed
  target. The project's friendliness charter is that the user enjoys
  CaraOS on the silicon they bought. If the OS itself eats meaningful
  fractions of the machine, that promise breaks.
- 128 MiB ≈ 6% of 2 GiB. It is generous for a SASOS kernel — Exec
  fit in single-digit megabytes — but tight enough to forbid casual
  bloat. The remaining ~1.9 GiB is the user's: Gleasanna, file
  caches, GPU surfaces, application working sets.
- No swap removes an entire category of complexity (page-out paths,
  swap accounting, eviction policy) and an entire category of bad
  behaviour (latency spikes, swap death). The SASOS layout in
  `docs/ARCHITECTURE.md` §4 already assumes that virtual addresses
  back stable physical residency; this principle makes that
  assumption explicit and binding.
- It forces every decision about resident state — caches, font
  atlases, kept-warm allocations, debug ring buffers — to be
  quantified. Code that wants memory has to justify why it lives.

How to apply: the kernel image, the resident library text/rodata,
the kernel heap high-water mark, and every always-resident driver or
service together respect the 128 MiB envelope. New always-resident
state earns its share of that envelope or it does not merge.
Periodic measurement (a `Stiur Mem`-style report broken down by
component) lands as soon as there is enough kernel to measure;
budget tracking goes into CI alongside it.

---

## 5. QEMU-first development

The default development loop is:

```
edit → build → qemu-system-riscv64 -machine virt …
```

OpenSBI ships with QEMU; UEFI is the boot mode; the same `splanc.efi` +
`croi.elf` artefacts that boot on real hardware boot in QEMU. The FDT
parser is what makes this work — QEMU `virt`'s MMIO map differs from the
X1's, but both ship a DTB.

The same model applies per architecture: once the ARM64 backend (epic H,
`docs/ARCH_HAL.md`) lands, its loop is `qemu-system-aarch64 -M virt …` (PSCI
in place of SBI, a PL011 console), driven by the same FDT-discovery
discipline. One `CARA_ARCH` per build directory.

Move to OrangePi RV2 hardware only when:

- QEMU's model can't reproduce a peripheral or behaviour we're testing.
- We're validating the final SD-card / NVMe boot path on silicon.
- We're chasing a bug that only manifests on hardware.

Regressions that "work on the board but fail in QEMU" are bugs unless we
have a *documented* reason QEMU can't model the case. Don't reach for
the SD card to make a CI failure go away.

Practical implications:

- `tests/data/` keeps two captured DTBs: `qemu-virt.dtb` and `x1.dtb`.
  Every FDT-parser unit test runs against both.
- The build system produces an ESP image that boots under
  `qemu-system-riscv64 -machine virt -bios default -drive
  file=esp.img,if=virtio` and on the RV2 with no recompile.
- A serial-line capture from QEMU is the first artefact attached to any
  bug report. The board-side capture is the second.

---

## 6. Phase discipline

Work belongs to a phase. Phases are ordered and we don't skip ahead.

The nine phases and their success criteria are in `docs/ROADMAP.md`.
The short version:

1. Boot to Clar from SD; USB mouse + keyboard work.
2. Boot from NVMe via a modernised FFS replacement (CaraFS).
3. AmigaOS Release 2 (V36+) functional parity, per the 3rd Edition RKMs.
4. GPU (cleanroom + microcode blob exception).
5. SBC peripheral coverage for hobby developers / gamers.
6. On-target C23 toolchain — the "modern Lattice-C" experience: a
   non-optimising compiler, assembler, linker, archiver, and build
   driver running on CaraOS, intended for on-machine hobby
   development. *Not* parity with the host build (host clang remains
   how CaraOS is built; for optimised code use the host).
7. Basic web experience — HTML5 / CSS / HTTP / HTTPS browser, **no
   JavaScript**. From the WHATWG / RFC / OpenType specs; no third-party
   browser engines.
8. Deluxe Paint for 2026 — a flagship cleanroom paint application
   keeping the DPaint workflow ergonomics, modernised (RGBA, layers,
   high-DPI canvases, deep undo, pen pressure).
9. 68k → RV64 binary translator — Rosetta-ish AOT/JIT for AmigaOS 1.3
   and 2.x Hunk executables, with library-call remapping to Phase 3
   entry points. **No hardware bangers** (custom chips, CIA, vector
   patches) — those programs are detected and rejected at load.

Inside a phase we may parallelise. Across phases we don't: a Phase 5
audio idea before Clar boots is out of phase. Park it.

---

## See also

- `LICENSE` — full BSD-2-Clause text
- `docs/ROADMAP.md` — the nine phases in detail
- `docs/ARCHITECTURE.md` — how the system is built (orthogonal to
  this document; principles constrain architecture, not vice versa)
