#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
#
# End-to-end smoke test for Croi via QEMU's -kernel handoff (no Splanc yet).
#
# Args:
#   $1  qemu-system-riscv64 binary
#   $2  path to croi.elf

set -eu

QEMU="$1"
KERNEL="$2"

if [[ ! -x "${QEMU}" && -z "$(command -v "${QEMU}" || true)" ]]; then
    echo "smoke_qemu_kernel: qemu binary not found: ${QEMU}" >&2
    exit 77
fi
if [[ ! -f "${KERNEL}" ]]; then
    echo "smoke_qemu_kernel: kernel image not found: ${KERNEL}" >&2
    exit 1
fi

LOG="$(mktemp)"
NVME_IMG="$(mktemp)"
trap 'rm -f "${LOG}" "${NVME_IMG}"' EXIT

# Throwaway 16 MiB raw disk for the NVMe bring-up + write/readback
# kernel tests (docs/PHASE2_NVME.md). Recreated per run, so the
# nvme_io test's pattern writes are never persisted anywhere real.
dd if=/dev/zero of="${NVME_IMG}" bs=1m count=16 2>/dev/null \
    || dd if=/dev/zero of="${NVME_IMG}" bs=1M count=16 2>/dev/null

# 8s ought to be plenty for a single-hart hello-world; the kernel WFIs
# after printing so qemu won't exit on its own — we kill it via timeout.
# -device qemu-xhci attaches a real xHCI 1.0 controller via PCIe so
# the kernel's PCIe enumeration (Croi_Pci_Init) discovers it; usb-kbd
# and usb-mouse plug into that controller and produce HID events
# once the xHCI driver path lands. Boot timeout bumped to give the
# scheduler / test runner room when more devices show up.
timeout --foreground -s TERM 10 "${QEMU}" \
    -M virt -m 256 -nographic -bios default \
    -kernel "${KERNEL}" \
    -device qemu-xhci \
    -device usb-kbd \
    -device usb-mouse \
    -drive "file=${NVME_IMG},if=none,format=raw,id=nvme0" \
    -device nvme,drive=nvme0,serial=cara-nvme-0 \
    > "${LOG}" 2>&1 || true

# Required magic strings, in order.
fail=0
for needle in \
    "Hello from Croi (SBI)" \
    "Hello from Croi (NS16550)" \
    "kernel tests:" \
    ; do
    if ! grep -qF -- "${needle}" "${LOG}"; then
        echo "smoke_qemu_kernel: FAIL: missing '${needle}'" >&2
        fail=1
    fi
done

# Assert the test runner reported zero failures. The line is of the form
# "kernel tests: N passed, M failed" — extract M and check.
if grep -qE 'kernel tests:.*[0-9]+ passed.*[0-9]+ failed' "${LOG}"; then
    failed_count=$(grep -oE 'kernel tests:.*[0-9]+ passed.*[0-9]+ failed' "${LOG}" \
                   | sed -E 's/.* ([0-9]+) failed.*/\1/' | tail -n1)
    if [[ "${failed_count}" != "0" ]]; then
        echo "smoke_qemu_kernel: FAIL: ${failed_count} kernel test(s) failed" >&2
        grep -E 'test:.* (PASS|FAIL)' "${LOG}" >&2
        fail=1
    fi
fi

if [[ ${fail} -ne 0 ]]; then
    echo "----- qemu stdio -----" >&2
    cat "${LOG}" >&2
    echo "----------------------" >&2
    exit 1
fi

echo "smoke_qemu_kernel: ok"
