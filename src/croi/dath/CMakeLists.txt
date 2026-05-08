# SPDX-License-Identifier: BSD-2-Clause
#
# cara_dath: dath.library core — framebuffer abstraction + drawing
# primitives. Phase 1 covers the simple-framebuffer flow; the Phase 4
# RTG-flavoured driver model layers under this same API surface.

add_library(cara_dath STATIC
    framebuffer.c
    draw.c
)
target_link_libraries(cara_dath
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)
