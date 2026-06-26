#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
#
# Boot smoke test for the AArch64 croi via QEMU's -kernel handoff (epic H.7.1,
# docs/ARM64.md). H.7.1 is boot-to-print: the kernel reaches EL1, brings up the
# PL011 console, prints a banner, and halts. This checks the banner appears.
# As later H.7.x slices reach the in-kernel test runner, this grows toward the
# rv64 smoke's "0 failed" assertion.
#
# Args:
#   $1  qemu-system-aarch64 binary
#   $2  path to croi.elf (the CARA_ARCH=arm64 build)

set -eu

QEMU="$1"
KERNEL="$2"

if [[ ! -x "${QEMU}" && -z "$(command -v "${QEMU}" || true)" ]]; then
    echo "smoke_qemu_arm64: qemu binary not found: ${QEMU}" >&2
    exit 77
fi
if [[ ! -f "${KERNEL}" ]]; then
    echo "smoke_qemu_arm64: kernel image not found: ${KERNEL}" >&2
    exit 1
fi

LOG="$(mktemp)"
trap 'rm -f "${LOG}"' EXIT

# The kernel halts (wfi loop) after printing, so QEMU won't exit on its own —
# timeout kills it. A 64-bit CPU must be named explicitly (the virt default is
# 32-bit-only).
timeout --foreground -s TERM 10 "${QEMU}" \
    -M virt -cpu cortex-a72 -m 256 -nographic \
    -kernel "${KERNEL}" \
    > "${LOG}" 2>&1 || true

if ! grep -qF "CaraOS arm64 boot: ok" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: missing 'CaraOS arm64 boot: ok'" >&2
    echo "----- boot stdio -----" >&2
    cat "${LOG}" >&2
    echo "----------------------" >&2
    exit 1
fi

echo "smoke_qemu_arm64: ok (EL1 boot + PL011 console)"
