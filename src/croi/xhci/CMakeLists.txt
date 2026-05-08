# SPDX-License-Identifier: BSD-2-Clause
#
# cara_xhci: kernel-internal xHCI host controller driver. Sits on
# top of cara_pci's PciFunction discovery + BAR allocator. The V36+
# public face (an `usb.device` analogue) trampolines into Croi_Xhci_*
# in Phase 3.

add_library(cara_xhci STATIC init.c setup.c ports.c slots.c transfer.c dispatch.c endpoint.c)
target_link_libraries(cara_xhci
    PUBLIC  cara_headers
    PRIVATE cara_kernel_flags cara_pci cara_mm cara_log
)
