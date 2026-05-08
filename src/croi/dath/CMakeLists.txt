# SPDX-License-Identifier: BSD-2-Clause
#
# cara_dath: dath.library core — framebuffer abstraction, drawing
# primitives, and simple-framebuffer FDT discovery. Phase 1 covers the
# simple-framebuffer flow; the Phase 4 RTG-flavoured driver model
# layers under this same API surface.
#
# Builds dual-target so host unit tests can verify FDT parsing without
# pulling in the kernel.

add_library(cara_dath STATIC
    framebuffer.c
    draw.c
    fdt_simple.c
)
target_include_directories(cara_dath PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(CARA_TARGET STREQUAL "riscv64")
    target_link_libraries(cara_dath
        PUBLIC cara_headers cara_fdt
        PRIVATE cara_kernel_flags
    )
else()
    target_link_libraries(cara_dath
        PUBLIC cara_headers cara_fdt
        PRIVATE cara_flags
    )
endif()
