# SPDX-License-Identifier: BSD-2-Clause
#
# cara_pci: PCIe FDT discovery + ECAM config-space accessors + bus
# walk. Brand-namespace; the V36+ public face (an
# expansion.library analogue) trampolines into Croi_Pci_*.
#
# Dual-target so a host unit test (test_pci_fdt) can verify the FDT
# parser against tests/data/qemu-virt.dtb without a kernel.

if(CARA_TARGET STREQUAL "host")
    add_library(cara_pci STATIC fdt.c)
else()
    add_library(cara_pci STATIC fdt.c config.c enum.c bar.c)
endif()
target_link_libraries(cara_pci
    PUBLIC cara_headers cara_fdt
    PRIVATE
        $<$<STREQUAL:${CARA_TARGET},riscv64>:cara_kernel_flags>
        $<$<STREQUAL:${CARA_TARGET},host>:cara_flags>
)
