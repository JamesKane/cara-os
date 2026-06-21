# SPDX-License-Identifier: BSD-2-Clause
#
# cara_devices: the kernel device drivers (L6) — timer/console/input
# CaraDevices registered with the exec device registry (src/croi/exec_lib/
# device.c) at boot. Kernel-only (riscv64); each driver's Croi_*_Init is
# called from entry.c, which pulls the TU + its static CaraDevice in.

add_library(cara_devices STATIC
    timer_device.c
    console_device.c
)
target_link_libraries(cara_devices
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)
