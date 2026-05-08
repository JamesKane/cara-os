# SPDX-License-Identifier: BSD-2-Clause
#
# cara_mm: physical memory management. Phase 1 ships PhysMap + the bitmap
# page allocator + the kernel heap; later phases add per-task page tables
# and ASID rotation. Builds for both host (for unit tests) and rv64.

add_library(cara_mm STATIC
    physmap.c
    page_alloc.c
    heap.c
)
target_include_directories(cara_mm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(CARA_TARGET STREQUAL "riscv64")
    target_link_libraries(cara_mm
        PUBLIC cara_headers cara_fdt
        PRIVATE cara_kernel_flags
    )
else()
    target_link_libraries(cara_mm
        PUBLIC cara_headers cara_fdt
        PRIVATE cara_flags
    )
endif()
