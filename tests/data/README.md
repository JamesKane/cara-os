# tests/data

Captured device tree blobs and other binary fixtures used by hosted unit
tests. These files are checked in so the test suite is self-contained:
running `ctest` does not require QEMU, the linux-orangepi tree, or any
cross-compiler.

## qemu-virt.dtb

Dumped from `qemu-system-riscv64 -machine virt`. This is the daily-driver
target for development. Used by `tests/unit/test_fdt_qemu.c` to validate
the FDT parser end-to-end against a known-good DTB.

Regenerate with:

    ./tools/dtb-capture/capture.sh

(QEMU 7.2+ produces a stable DTB layout; expect this file to change rarely.)

## x1.dtb (deferred)

Not yet checked in. The OrangePi RV2 (Spacemit Ky X1) device tree comes
from `arch/riscv/boot/dts/ky/x1_orangepi-rv2.dts` on the linux-orangepi
`orange-pi-6.6-ky` branch. Building it requires running the full kernel
DTB pipeline (cpp + dt-bindings headers + dtc), which depends on a riscv64
cross-toolchain that isn't installed yet for the OS build itself.

Tracked: x1.dtb regeneration will land alongside the toolchain work in
Phase 1 slice 2. See `tools/dtb-capture/capture.sh` for the current
manual procedure.
