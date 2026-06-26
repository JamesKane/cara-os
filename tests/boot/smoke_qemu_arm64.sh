#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
#
# Boot smoke test for the AArch64 croi via QEMU's -kernel handoff (epic H.7,
# docs/ARM64.md). H.7.2 brought up stage-1 paging (kernel runs in the SASOS
# upper half); H.7.2b parses the real device tree and initialises the physical
# memory manager (page allocator + heap) through the shared cara_fdt/cara_mm
# code. This checks the banner appears, that the printed code address is
# upper-half (MMU on), and that mm init reached the "mm up" milestone. As later
# H.7.x slices reach the in-kernel test runner, this grows toward the rv64
# smoke's "0 failed" assertion.
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

fail=0
if ! grep -qF "CaraOS arm64 boot: ok" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: missing 'CaraOS arm64 boot: ok'" >&2
    fail=1
fi
# Proof the MMU is on + we run in the upper half: the kernel prints its own
# code address, which must carry the SASOS upper-half prefix.
if ! grep -qiE "arm64_kernel_main @ 0xffffffc0" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: code address is not upper-half (MMU not on?)" >&2
    fail=1
fi
# Proof the portable FDT + mm path ran (parsed the DTB, inited the allocator).
if ! grep -qF "mm up" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: mm bring-up did not complete" >&2
    fail=1
fi

if [[ ${fail} -ne 0 ]]; then
    echo "----- boot stdio -----" >&2
    cat "${LOG}" >&2
    echo "----------------------" >&2
    exit 1
fi

echo "smoke_qemu_arm64: ok (paging + FDT + page allocator + heap)"
