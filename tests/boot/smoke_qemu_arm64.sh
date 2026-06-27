#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
#
# Boot smoke test for the AArch64 croi via QEMU's -kernel handoff (epic H.7,
# docs/ARM64.md). Progressively asserts what each slice reaches:
#   H.7.2  stage-1 paging — the kernel runs in the SASOS upper half (its printed
#          code address carries the 0xffffffc0_ prefix).
#   H.7.2b FDT parse + physical memory manager (page allocator + heap) via the
#          shared cara_fdt/cara_mm code ("mm up").
#   H.7.3  trap + syscall — an svc round-trips through VBAR_EL1 + the portable
#          Croi_TrapDispatch ("trap: svc ok").
#   H.7.4  per-task page tables, context switch, FP, and the EL1<->EL0 round-trip.
#   H.7.5  timer + GICv2 IRQ ("timer: ticks ok") + a clean PSCI power-off (so the
#          kernel exits QEMU instead of waiting for the timeout).
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
# Proof the portable FDT + mm path ran: the page allocator + heap came up.
if ! grep -qF "free pages" "${LOG}" || ! grep -qF "Croi_Alloc(128)" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: mm bring-up did not complete" >&2
    fail=1
fi
# Proof the trap + syscall path works: an svc round-tripped through VBAR_EL1 +
# the portable Croi_TrapDispatch and returned the expected value.
if ! grep -qF "trap: svc ok" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: svc trap round-trip did not complete" >&2
    fail=1
fi
# Proof per-task page tables work: a Page_Map'd page activated via TTBR0 and
# round-tripped through both TTBR0 and the TTBR1 direct map.
if ! grep -qF "pagetable: map ok" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: per-task page-table round-trip did not complete" >&2
    fail=1
fi
# Proof the context-switch primitive works: switched to a second context and back.
if ! grep -qF "ctxsw: round-trip ok" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: context-switch round-trip did not complete" >&2
    fail=1
fi
# Proof the NEON file is saved/restored across a switch (and FP is enabled).
if ! grep -qF "fp: preserved ok" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: FP not preserved across context switch" >&2
    fail=1
fi
# Proof enter-U-mode works: eret to EL0, the stub svc'd back, exit code returned.
if ! grep -qF "enter-U-mode: ok" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: EL0 enter/return round-trip did not complete" >&2
    fail=1
fi
# Proof the timer + GIC IRQ path + portable time layer work: the one-shot
# deadline API drove several CNTV interrupts through Croi_Time_OnTimerTrap.
if ! grep -qF "timer: deadlines ok" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: timer/IRQ deadlines did not arrive" >&2
    fail=1
fi
# Proof structured logging works: a LOG_* record reached the console sink.
if ! grep -qF "arm64 logging up" "${LOG}"; then
    echo "smoke_qemu_arm64: FAIL: cara_log did not emit through the sink" >&2
    fail=1
fi

if [[ ${fail} -ne 0 ]]; then
    echo "----- boot stdio -----" >&2
    cat "${LOG}" >&2
    echo "----------------------" >&2
    exit 1
fi

echo "smoke_qemu_arm64: ok (paging + FDT + mm + traps + PT + ctxsw + fp + EL0 + timer/IRQ + PSCI)"
