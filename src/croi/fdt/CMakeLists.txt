# SPDX-License-Identifier: BSD-2-Clause
#
# cara_fdt: the FDT (device tree) parser. Read-only, allocation-free, host-
# buildable. The same .c files link into the kernel image and into hosted
# unit tests; the only differences between targets are the flags that come
# from cara_flags / freestanding flags applied at link time.

add_library(cara_fdt STATIC
    fdt.c
    fdt_props.c
    fdt_path.c
)
target_include_directories(cara_fdt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(CARA_TARGET STREQUAL "riscv64")
    target_link_libraries(cara_fdt PUBLIC cara_headers PRIVATE cara_kernel_flags)
else()
    target_link_libraries(cara_fdt PUBLIC cara_headers PRIVATE cara_flags)
endif()
