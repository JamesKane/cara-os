#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
#
# End-to-end smoke test for Croi via QEMU's -kernel handoff (no Splanc yet).
#
# Boots the kernel twice against the SAME NVMe image: the first boot
# formats CaraFS and seeds a marker file; the second boot mounts the
# persisted volume and verifies the marker — proving F5 reboot
# persistence (write → reboot → still there).
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

LOG1="$(mktemp)"
LOG2="$(mktemp)"
NVME_IMG="$(mktemp)"
trap 'rm -f "${LOG1}" "${LOG2}" "${NVME_IMG}"' EXIT

# A 16 MiB raw disk for the NVMe bring-up + CaraFS tests. Created once
# and reused across both boots so CaraFS state persists between them.
dd if=/dev/zero of="${NVME_IMG}" bs=1m count=16 2>/dev/null \
    || dd if=/dev/zero of="${NVME_IMG}" bs=1M count=16 2>/dev/null

# Boot the kernel once, capturing stdio to $1. The kernel WFIs after the
# test runner, so qemu won't exit on its own — timeout kills it.
# -device qemu-xhci attaches a real xHCI 1.0 controller via PCIe so the
# kernel's PCIe enumeration discovers it; usb-kbd/usb-mouse plug into it.
# The nvme device persists to ${NVME_IMG}; a guest NVMe Flush (CaraFS
# Sync) commits it so the second boot sees the seeded marker.
run_boot() {
    local log="$1"
    timeout --foreground -s TERM 10 "${QEMU}" \
        -M virt -m 256 -nographic -bios default \
        -kernel "${KERNEL}" \
        -device qemu-xhci \
        -device usb-kbd \
        -device usb-mouse \
        -drive "file=${NVME_IMG},if=none,format=raw,id=nvme0" \
        -device nvme,drive=nvme0,serial=cara-nvme-0 \
        > "${log}" 2>&1 || true
}

fail=0

# Required banner strings, plus "0 failed" from the test runner.
check_boot() {
    local log="$1" label="$2"
    local needle
    for needle in \
        "Hello from Croi (SBI)" \
        "Hello from Croi (NS16550)" \
        "kernel tests:" \
        ; do
        if ! grep -qF -- "${needle}" "${log}"; then
            echo "smoke_qemu_kernel: FAIL (${label}): missing '${needle}'" >&2
            fail=1
        fi
    done
    # Subgoal 3: S/Startup-Sequence executed at boot (Echo CaraOS-ready).
    if ! grep -qF "startup: CaraOS-ready" "${log}"; then
        echo "smoke_qemu_kernel: FAIL (${label}): S/Startup-Sequence did not run" >&2
        fail=1
    fi
    if grep -qE 'kernel tests:.*[0-9]+ passed.*[0-9]+ failed' "${log}"; then
        local failed_count
        failed_count=$(grep -oE 'kernel tests:.*[0-9]+ passed.*[0-9]+ failed' "${log}" \
                       | sed -E 's/.* ([0-9]+) failed.*/\1/' | tail -n1)
        if [[ "${failed_count}" != "0" ]]; then
            echo "smoke_qemu_kernel: FAIL (${label}): ${failed_count} kernel test(s) failed" >&2
            grep -E 'test:.* (PASS|FAIL)' "${log}" >&2
            fail=1
        fi
    fi
}

# Boot 1: fresh image → CaraFS formats itself and seeds the marker.
run_boot "${LOG1}"
check_boot "${LOG1}" "boot1"
if ! grep -qF "persist marker seeded" "${LOG1}"; then
    echo "smoke_qemu_kernel: FAIL (boot1): CaraFS marker was not seeded" >&2
    fail=1
fi
if ! grep -qF "saved note" "${LOG1}"; then
    echo "smoke_qemu_kernel: FAIL (boot1): Clar did not save its drawer file" >&2
    fail=1
fi

# Boot 2: same image → CaraFS mounts the persisted volume and verifies.
run_boot "${LOG2}"
check_boot "${LOG2}" "boot2"
if ! grep -qF "persist marker verified across reboot" "${LOG2}"; then
    echo "smoke_qemu_kernel: FAIL (boot2): CaraFS marker did not persist across reboot" >&2
    fail=1
fi
# Phase 2 success criterion: Clar saved "as" into its drawer's CaraFS
# file on boot 1; boot 2 must read it back. (clar_smoke types 'a','s'.)
if ! grep -qF "drawer note='as'" "${LOG2}"; then
    echo "smoke_qemu_kernel: FAIL (boot2): Clar's drawer file did not persist across reboot" >&2
    fail=1
fi

if [[ ${fail} -ne 0 ]]; then
    echo "----- boot 1 stdio -----" >&2
    cat "${LOG1}" >&2
    echo "----- boot 2 stdio -----" >&2
    cat "${LOG2}" >&2
    echo "------------------------" >&2
    exit 1
fi

echo "smoke_qemu_kernel: ok (CaraFS + Clar's drawer file persisted across reboot)"
