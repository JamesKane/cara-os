# SPDX-License-Identifier: BSD-2-Clause
#
# cara_nvme: kernel-internal NVMe block driver. Sits on top of
# cara_pci's PciFunction discovery + BAR allocator. The V36+ public
# face (dos.library via Logaic, later an `nvme.device`) trampolines
# into Croi_Nvme_* in Phase 2 Subgoal 3 / Phase 3.

add_library(cara_nvme STATIC init.c queue.c identify.c io.c)
target_link_libraries(cara_nvme
    PUBLIC  cara_headers
    PRIVATE cara_kernel_flags cara_pci cara_mm cara_log
)
