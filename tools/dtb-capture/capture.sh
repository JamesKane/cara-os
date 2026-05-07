#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
#
# Regenerate the captured device tree blobs that live in tests/data/.
# Run from the repo root: ./tools/dtb-capture/capture.sh
#
# Two DTBs are produced:
#   qemu-virt.dtb  - dumped from qemu-system-riscv64 -machine virt
#   x1.dtb         - built from the linux-orangepi orange-pi-6.6-ky branch
#                    (requires LINUX_ORANGEPI_TREE pointing at a clone)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEST="${REPO_ROOT}/tests/data"
mkdir -p "${DEST}"

echo "==> qemu-virt.dtb"
if ! command -v qemu-system-riscv64 >/dev/null; then
    echo "qemu-system-riscv64 not found; skipping qemu-virt.dtb" >&2
else
    qemu-system-riscv64 \
        -machine "virt,dumpdtb=${DEST}/qemu-virt.dtb" \
        -nographic >/dev/null 2>&1 || true
    if [[ -s "${DEST}/qemu-virt.dtb" ]]; then
        echo "  wrote ${DEST}/qemu-virt.dtb ($(wc -c < "${DEST}/qemu-virt.dtb") bytes)"
    else
        echo "  qemu produced no output; skipping" >&2
    fi
fi

echo "==> x1.dtb"
LINUX="${LINUX_ORANGEPI_TREE:-}"
if [[ -z "${LINUX}" || ! -d "${LINUX}" ]]; then
    cat >&2 <<'EOF'
  LINUX_ORANGEPI_TREE not set or not a directory.
  Set it to a clone of https://github.com/orangepi-xunlong/linux-orangepi.git
  with the orange-pi-6.6-ky branch fetched, e.g.:

      LINUX_ORANGEPI_TREE=$HOME/Development/linux-orangepi \
          ./tools/dtb-capture/capture.sh

  x1.dtb regen requires the full kernel build (cpp + dtc with kernel
  include paths). Skipping for now.
EOF
    exit 0
fi

cd "${LINUX}"
if ! git rev-parse --verify origin/orange-pi-6.6-ky >/dev/null 2>&1; then
    echo "  origin/orange-pi-6.6-ky not present in ${LINUX}; skipping" >&2
    exit 0
fi

# Building x1.dtb requires running cpp with kernel-internal include paths
# and dtc against the preprocessed result. The simplest way is to drive
# the kernel's own dtbs target. That requires a riscv64 cross-compiler
# installed and is out of scope for this script. Emit instructions:
cat >&2 <<'EOF'
  x1.dtb regeneration via the kernel build is not yet wired into this
  script. To do it manually:

      cd $LINUX_ORANGEPI_TREE
      git checkout orange-pi-6.6-ky
      make ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- x1_defconfig
      make ARCH=riscv CROSS_COMPILE=riscv64-unknown-elf- dtbs
      cp arch/riscv/boot/dts/ky/x1_orangepi-rv2.dtb \
          $REPO_ROOT/tests/data/x1.dtb

  This will be folded into capture.sh once the cross-toolchain story for
  the OS itself stabilises (Phase 1 slice 2).
EOF
