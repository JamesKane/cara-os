# SPDX-License-Identifier: BSD-2-Clause
#
# cara_mm: physical memory management. Phase 1 ships PhysMap + the bitmap
# page allocator + the kernel heap; later phases add per-task page tables
# and ASID rotation. Builds for both host (for unit tests) and rv64.

add_library(cara_mm STATIC
    physmap.c
    page_alloc.c
    heap.c
    pt.c
)
target_include_directories(cara_mm PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(CARA_TARGET STREQUAL "riscv64")
    # Any kernel build (riscv64 or arm64 ISA) uses the freestanding flags.
    target_link_libraries(cara_mm
        PUBLIC cara_headers cara_fdt
        PRIVATE cara_kernel_flags
    )
    # shared.c (SASOS shared heap) uses satp / sfence — RISC-V kernel only.
    # The AArch64 equivalent (TTBR-based shared mapping) lands with the
    # arm64 mm runtime (epic H.7, docs/ARM64.md).
    if(CARA_ARCH STREQUAL "riscv64")
        target_sources(cara_mm PRIVATE shared.c)
    endif()
else()
    target_link_libraries(cara_mm
        PUBLIC cara_headers cara_fdt
        PRIVATE cara_flags
    )
endif()
