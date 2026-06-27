# SPDX-License-Identifier: BSD-2-Clause
#
# cara_log: structured logging — log records, ring buffer, sink dispatch,
# Croi_Log entry point, and the NS16550 sink. Kernel-only for now; if a
# future host test wants the formatter we'll split log.c.

add_library(cara_log STATIC
    log.c
)
# The NS16550 sink calls the RISC-V kernel's ns16550 driver (not in the arm64
# link, which uses a PL011 console). Build it only for the RISC-V backend; the
# AArch64 kernel registers its own console sink.
if(CARA_ARCH STREQUAL "riscv64")
    target_sources(cara_log PRIVATE sink_ns16550.c)
endif()
target_include_directories(cara_log PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(cara_log
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)
