# SPDX-License-Identifier: BSD-2-Clause
#
# cara_kobj: kernel object bases and per-task handle tables.

add_library(cara_kobj STATIC
    kobj.c
    handle_table.c
)
target_link_libraries(cara_kobj
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)
