# OrangePi RV2 / Spacemit-Ky X1 hardware snapshot

> **This file is a derived reference, not a source of truth.** All numbers
> below are extracted from the linux-orangepi vendor tree, branch
> `orange-pi-6.6-ky`, files under `arch/riscv/boot/dts/ky/`. Each entry
> below cites the file and node it came from. **CaraOS code does not
> hard-code any of these values** — they are discovered at boot from the
> FDT (see `docs/ARCHITECTURE.md` §9 and `docs/DTS_PARSER.md`). This
> document exists for human reading: when you are debugging a fault and
> want to know what `0xD401_7000` is, look here.

Snapshot taken: 2026-05-07.

---

## SoC: Spacemit / Ky X1

Source: `arch/riscv/boot/dts/ky/x1.dtsi`

| Property              | Value                                 |
|-----------------------|---------------------------------------|
| Top-level `compatible`| `"ky,x1"`                             |
| `#address-cells`      | 2                                     |
| `#size-cells`         | 2                                     |
| MMU                   | `riscv,sv39`                          |
| Timebase (board)      | 24 MHz (overridden in RV2 board DTS)  |

### Cores

8× `"ky,x60"` (also `"riscv"`), arranged as 2 clusters of 4:

| Hartid | Cluster | Compatible      |
|--------|---------|-----------------|
| 0..3   | 0       | `ky,x60` / `riscv` |
| 4..7   | 1       | `ky,x60` / `riscv` |

Per-hart ISA (identical for all 8):

```
riscv,isa            = "rv64imafdcv"
riscv,isa-base       = "rv64i"
riscv,isa-extensions = "i", "m", "a", "f", "d", "c", "v",
                       "zicbom", "zicboz", "zicntr", "zicond",
                       "zicsr", "zifencei", "zihintpause", "zihpm",
                       "zfh", "zfhmin", "zba", "zbb", "zbc", "zbs",
                       "zkt", "zvfh", "zvfhmin", "zvkt", "sscofpmf",
                       "sstc", "svinval", "svnapot", "svpbmt"
```

L1 cache, per hart:

| Property                | Value     |
|-------------------------|-----------|
| L1 I-cache size         | 32 KiB    |
| L1 I-cache line / block | 64 B      |
| L1 I-cache sets         | 128       |
| L1 D-cache size         | 32 KiB    |
| L1 D-cache line / block | 64 B      |
| L1 D-cache sets         | 128       |
| `riscv,cbom-block-size` | 64 B      |
| `riscv,cboz-block-size` | 64 B      |

Implications used by CaraOS:

- **RVV is baseline.** The V extension is in `rv64imafdcv`; we plan for it.
- **Sstc is present.** S-mode programs `stimecmp` directly.
- **Svnapot, Svinval, Svpbmt** all present — usable for huge-page coalescing,
  scoped fences, and PMA overrides respectively.
- **Cache line padding** in our shared atomics uses 64 B (matches CBOM).

---

## Board: OrangePi RV2

Source: `arch/riscv/boot/dts/ky/x1_orangepi-rv2.dts`

| Property         | Value                                  |
|------------------|----------------------------------------|
| `model`          | `"ky x1 orangepi-rv2 board"`           |
| `compatible`     | `"ky,orangepi-rv2", "ky,x1"`           |
| Timebase         | 24 MHz                                 |

### Memory

Two banks, 2 GiB each = 4 GiB total:

| Range                                           | Size     |
|-------------------------------------------------|----------|
| `0x0000_0000_0000_0000` – `0x0000_0000_7FFF_FFFF` | 2 GiB    |
| `0x0000_0001_0000_0000` – `0x0000_0001_7FFF_FFFF` | 2 GiB    |

Reserved regions (must not allocate from):

| Range                                            | Size      | Purpose         |
|--------------------------------------------------|-----------|-----------------|
| `0x0000_0000_4000_0000` – `0x0000_0000_6FFF_FFFF` (CMA `alloc-ranges`) | up to 768 MiB | `linux,cma` pool, alignment 1 MiB, **size 384 MiB** |
| `0x0000_0000_2FF4_0000` – `0x0000_0000_2FFF_FFFF` | 768 KiB   | `dpu_reserved` (DPU MMU + cmdlist) |

CaraOS will **reserve** the CMA `size` (384 MiB) and the entire `dpu_reserved`
region from its frame allocator, mirroring vendor expectations. CMA
itself is a Linux-ism we don't need; we just refuse to allocate inside it.

### Chosen / boot

```
chosen {
    bootargs    = "earlycon=sbi console=ttyS0,115200n8 loglevel=8 swiotlb=65536 rdinit=/init";
    stdout-path = "serial0:115200n8";
};
```

`serial0` is an alias for `&uart0` (see below).

`earlycon=sbi` indicates that, before any in-tree UART driver attaches,
the console is SBI-printed via `sbi_debug_console_write` /
`sbi_legacy_putchar`. CaraOS Splanc and early Croi use the same: SBI for
the first messages, then the real UART once we've parsed its FDT node.

---

## MMIO

All addresses are physical. Source: `arch/riscv/boot/dts/ky/x1.dtsi`.

### Per-hart interrupt controllers

`compatible = "riscv,cpu-intc"` — the standard RISC-V local interrupt
controller. Trap causes 1/3/5/7/9/11 are routed to S-mode through
`scause`. No MMIO base — these are CSR-addressed.

### CLINT

| Property      | Value                            |
|---------------|----------------------------------|
| `compatible`  | `"riscv,clint0"`                 |
| Base          | `0xE400_0000`                    |
| Size          | `0x0001_0000` (64 KiB)           |
| `interrupts-extended` | per-hart 3 (msip), 7 (mtip) |

CaraOS uses CLINT for software IPIs across harts. Timer interrupts are
delivered via Sstc (`stimecmp`) instead of CLINT MTIMECMP, since Sstc is
present.

### PLIC

| Property               | Value                            |
|------------------------|----------------------------------|
| `compatible`           | `"riscv,plic0"`                  |
| Base                   | `0xE000_0000`                    |
| Size                   | `0x0400_0000` (64 MiB)           |
| `riscv,ndev`           | 159                              |
| `riscv,max-priority`   | 7                                |
| `interrupts-extended`  | per-hart 11 (S-ext), 9 (S-ext)... actually per-hart pair (ext, supervisor-ext) |

### UARTs

All UARTs are PXA-style 16550-compatible. `compatible = "ky,pxa-uart"`,
`reg-shift = <2>`, `reg-io-width = <4>` — i.e. byte registers are
addressed at 4-byte stride and accessed as 32-bit MMIO loads/stores.

| Alias    | Node          | Base           | Size  | IRQ |
|----------|---------------|----------------|-------|-----|
| serial0  | `uart0`       | `0xD401_7000`  | 0x100 | 42  |
| serial2  | `uart2`       | `0xD401_7100`  | 0x100 | TBD |
| serial3  | `uart3`       | `0xD401_7200`  | 0x100 | TBD |
| serial4  | `uart4`       | `0xD401_7300`  | 0x100 | TBD |
| serial5  | `uart5`       | `0xD401_7400`  | 0x100 | TBD |
| serial6  | `uart6`       | `0xD401_7500`  | 0x100 | TBD |
| serial7  | `uart7`       | `0xD401_7600`  | 0x100 | TBD |
| serial8  | `uart8`       | `0xD401_7700`  | 0x100 | TBD |
| serial9  | `uart9`       | `0xD401_7800`  | 0x100 | TBD |

`uart0` (`serial0`) is `chosen`'s `stdout-path`. CaraOS attaches its
console driver here.

The vendor `clk-fpga` property on `uart0` is `14_750_000` — that is the
emulator/FPGA clock and is **not** what the silicon clocks at. On real
silicon the clock comes from `&ccu CLK_UART1` via the clock-controller
node at `0xD405_0000`. Until the clock graph walker exists in the FDT
parser, the early console will program the divisor assuming the
canonical 14.745600 MHz reference (which produces standard baud rates
out-of-the-box). Validate against silicon before trusting.

### Pin controller

| Property      | Value                                                   |
|---------------|---------------------------------------------------------|
| `compatible`  | `"pinconf-single-aib"`                                  |
| Base          | `0xD401_E000` (size 0x250) + `0xD401_9800` + `0xD401_9000` |

Not v0-relevant — Croi never speaks to the pin controller; whatever
U-Boot mux'd is what we get. Listed for completeness.

### Clock controller

| Property      | Value                                       |
|---------------|---------------------------------------------|
| `compatible`  | `"ky,x1-clock"`                             |
| Base          | `0xD405_0000` + `0xD428_2800` (multi-region) |

Not v0-relevant beyond reading the UART input clock; deferred to a
post-v0 module.

---

## How to refresh this snapshot

```sh
cd /Users/jkane/Development/linux-orangepi
git show origin/orange-pi-6.6-ky:arch/riscv/boot/dts/ky/x1.dtsi > /tmp/x1.dtsi
git show origin/orange-pi-6.6-ky:arch/riscv/boot/dts/ky/x1_orangepi-rv2.dts > /tmp/rv2.dts
```

Cross-reference any change against this file. If a node moves, update
both the table here and (importantly) any captured DTB binaries in
`tests/data/` used by the FDT parser unit tests.

We do not pull the DTB at runtime from this tree — U-Boot on the board
embeds its own. This file's role is purely human cross-reference.
