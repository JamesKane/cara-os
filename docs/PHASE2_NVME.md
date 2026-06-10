# Phase 2 Subgoal 1 — NVMe driver

> Plan for the NVMe block driver that gives CaraFS (Phase 2
> Subgoal 2) a disk to live on. Pairs with `docs/ROADMAP.md`
> Phase 2 and `docs/PHASE1_USB.md` (the xHCI driver this one is
> structurally modelled on). Cleanroom sources: the **NVM Express
> Base Specification** (rev 1.4+) and the **NVM Command Set
> Specification** — no third-party NVMe code is read or linked
> (`docs/PRINCIPLES.md` §2). Section references in code comments
> cite those documents.

---

## Shape

The driver mirrors the xHCI layering exactly:

- `include/cara/nvme.h` — brand-namespace surface (`Croi_Nvme_*`),
  register/field constants from the spec, `struct NvmeController`.
- `src/croi/nvme/` — the implementation (`init.c`, `queue.c`,
  `identify.c`, `io.c`), private helpers in `internal.h`.
- Probe is driven from `entry.c` right after the xHCI block, against
  the first PCI function of class `01/08/02` (mass storage / NVM /
  NVMe I/O command set) in `g_pci_inv`.
- Verification is a `KERNEL_TEST` chain (`src/croi/tests/test_nvme.c`)
  asserting against the globals `g_nvme` / `g_nvme_probed`, plus the
  boot smoke harness growing `-device nvme` with a throwaway raw
  disk image.
- **Polled completion only** in this first cut, like Phase 1 xHCI.
  MSI-X / interrupt-driven completion is a later epic, after the
  same plumbing exists for xHCI.

The V36+ public face of storage is `dos.library` + the Logaic boot
path (Phase 2 Subgoal 3); nothing here is API-namespace. A future
`nvme.device` exec-device wrapper is Phase 3 territory.

### QEMU daily driver

```
truncate -s 16M /tmp/cara-nvme.img
qemu-system-riscv64 -M virt -m 256 -nographic -bios default \
    -kernel build-rv64/src/croi/croi.elf \
    -device qemu-xhci -device usb-kbd -device usb-mouse \
    -drive file=/tmp/cara-nvme.img,if=none,format=raw,id=nvme0 \
    -device nvme,drive=nvme0,serial=cara-nvme-0
```

QEMU's `nvme` device (vendor 0x1b36, device 0x0010) is a conformant
NVMe 1.4 controller on the PCIe bus — same ECAM enumeration path the
RV2's M.2 slot will use. No virtio shortcut, per PRINCIPLES §5.

---

## Epics

### N1 — PCI probe + controller reset

Find the function by class triple, `Croi_Pci_AllocateBar(0)` (BAR0 is
a 64-bit memory BAR; the Phase 1 MEM32-window allocation handles it),
map via `Mm_PhysToVirt`, parse `CAP` (MQES, DSTRD, TO, CSS, MPSMIN),
check `VS` ≥ 1.0, then run the disable sequence: `CC.EN=0` → wait
`CSTS.RDY=0` (timeout from `CAP.TO`, units of 500 ms).
`KERNEL_TEST(nvme_probe)` asserts the capability snapshot is sane.

### N2 — Admin queues + enable

One page each for ASQ (64 entries × 64 B) and ACQ (256 × 16 B fits,
but we size both at 64). Program `AQA`/`ASQ`/`ACQ`, set
`CC.IOSQES=6, IOCQES=4, MPS=0 (4 KiB), CSS=0 (NVM)`, `CC.EN=1`,
wait `CSTS.RDY=1`. Submit/complete primitive: bump SQ tail, ring
SQ0 tail doorbell, poll ACQ for the phase-matched CQE, match CID,
update CQ head doorbell. Status field != 0 is logged and returned
as `CARA_EIO`.

### N3 — Identify

Admin Identify (opcode 0x06) with a one-page DMA buffer:
- CNS 0x01 (controller): cache + log SN/MN/FR, NN.
- CNS 0x00 (namespace, NSID 1): NSZE, FLBAS → the active LBA format's
  LBADS; derive `block_bytes` and `n_blocks`.
`KERNEL_TEST(nvme_identify)` asserts NSID 1 exists with a non-zero
size and a 512-or-4096-byte block size.

### N4 — I/O queue pair

Create I/O CQ (admin 0x05) then I/O SQ (admin 0x01), QID 1, one page
each, physically contiguous (`PC=1`), interrupts disabled (`IEN=0` —
polled). Doorbell offsets honour `CAP.DSTRD`.

### N5 — Read/write

NVM Read (0x02) / Write (0x01) on QID 1. PRP1 + optional PRP2 covers
transfers up to two pages (8 KiB) — enough for CaraFS metadata blocks;
PRP lists are a follow-on epic when CaraFS wants bigger I/O.
`Croi_Nvme_Read/Write(c, nsid, lba, n_blocks, buf)` with page-aligned
kernel-direct-map buffers. `KERNEL_TEST(nvme_io)`: write a pattern to
a high LBA, read it back through a second buffer, compare; the smoke
harness's throwaway image makes this safe and deterministic.

### Later (parked, not Phase 2 Subgoal 1)

- PRP lists (> 8 KiB per command), namespace > 1, multiple I/O queues
  per hart.
- MSI-X interrupt-driven completion (alongside xHCI's UB.7).
- `nvme.device` exec-device surface (Phase 3).
- CaraFS itself — `docs/CARAFS.md` (Phase 2 Subgoal 2, next).

---

## Green gate

Same as Phase 1 (`docs/HANDOFF.md` §5) with one addition: the boot
smoke harness now creates a temp raw image and attaches
`-device nvme`, so the nvme KERNEL_TESTs are exercised by
`tests/boot/smoke_qemu_kernel.sh`. Running the kernel *without*
`-device nvme` fails the nvme tests — expected, same caveat as
running without the USB devices.
