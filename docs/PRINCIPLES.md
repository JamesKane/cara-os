# CaraOS engineering principles

These are the rules we agree to follow on every change. They sit *above*
the architecture: an architectural decision can change without rewriting
this document; a violation of one of these principles needs an explicit,
documented exception.

There are five principles. The first three are policy. The last two are
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
   the original sources into CaraOS. Naming follows the Irish-derived
   nomenclature (Croi, Logaic, Leargas, …) — see ARCHITECTURE.md §13.
2. **Hardware.** The Spacemit/Ky X1 hardware addresses come from the
   linux-orangepi vendor DTS. We *parse* the DTB at boot rather than
   transcribing constants — see ARCHITECTURE.md §9 and DTS_PARSER.md.
3. **Standards.** When we implement FAT, USB, NVMe, ATA, TCP/IP, or any
   other protocol: read the standard, write our own.

Read references, but write fresh. If a reference's code structure
demonstrably leaks into ours, that's a bug.

---

## 4. QEMU-first development

The default development loop is:

```
edit → build → qemu-system-riscv64 -machine virt …
```

OpenSBI ships with QEMU; UEFI is the boot mode; the same `splanc.efi` +
`croi.elf` artefacts that boot on real hardware boot in QEMU. The FDT
parser is what makes this work — QEMU `virt`'s MMIO map differs from the
X1's, but both ship a DTB.

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

## 5. Phase discipline

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
