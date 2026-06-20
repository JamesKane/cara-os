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
    text.c
    font_8x8.c
    console.c
)
target_include_directories(cara_dath PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(CARA_TARGET STREQUAL "riscv64")
    # Heap-backed bitmap helpers depend on Croi_Alloc/Free which live
    # in the kernel heap module — only built for the kernel target.
    target_sources(cara_dath PRIVATE bitmap.c)
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

# graphics.library (L4) — the V36+ API surface over the Dath rasteriser
# (docs/DATH_GRAPHICS.md). Kernel-only: it builds the library base + vec
# table and (later slices) the syscall trampolines + Croi_Gfx_*_Impl
# marshals onto Dath_*. Kept in a separate static lib (like cara_dos_lib)
# so cara_dath stays the pure dual-target rasteriser; whole-archived into
# croi by src/croi/CMakeLists.txt so the vec/trampolines aren't GC'd.
if(CARA_TARGET STREQUAL "riscv64")
    cara_lvo_gen_library("${CMAKE_SOURCE_DIR}/tools/lvo-gen/graphics.conf")

    add_library(cara_graphics_lib STATIC
        graphics_hooks.c
        graphics_lib.c
        trampolines.S
    )
    target_link_libraries(cara_graphics_lib
        PUBLIC cara_headers
        PRIVATE cara_kernel_flags
    )

    if(CARA_LVO_GEN_BIN)
        set(_gfx_vec_c "${CARA_LVO_GEN_OUT_DIR}/src/croi/dath/graphics_vec.c")
        target_sources(cara_graphics_lib PRIVATE "${_gfx_vec_c}")
        add_dependencies(cara_graphics_lib cara_graphics_lib_gen)
    endif()
endif()
