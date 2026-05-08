# SPDX-License-Identifier: BSD-2-Clause
#
# cara_loader: ELF loader for static U-mode binaries.

add_library(cara_loader STATIC
    elf.c
)
target_link_libraries(cara_loader
    PUBLIC cara_headers cara_mm
    PRIVATE cara_kernel_flags
)
