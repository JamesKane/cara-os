# SPDX-License-Identifier: BSD-2-Clause
#
# cara_hid: HID boot-protocol decoders + USB Usage → V36+ rawkey table.
# Pure logic, no I/O — builds for both host (unit tests) and rv64 (kernel
# substrate before the HID Gleas exists). Same source files in either
# build, distinguished only by which flags interface they pick up.

add_library(cara_hid STATIC hid.c)

if(CARA_TARGET STREQUAL "riscv64")
    target_link_libraries(cara_hid
        PUBLIC  cara_headers
        PRIVATE cara_kernel_flags
    )
else()
    target_link_libraries(cara_hid
        PUBLIC  cara_headers
        PRIVATE cara_flags
    )
endif()
