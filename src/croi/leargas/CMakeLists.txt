# SPDX-License-Identifier: BSD-2-Clause
#
# cara_leargas: brand-namespace Phase 1 minimum window-system
# substrate. L0 (input event ring) is pure logic — atomics + flat
# storage, no I/O — so the same source builds for host (unit tests)
# and rv64 (kernel). When LA/LB add framebuffer-touching code they
# may need to gate on CARA_TARGET like cara_dath does.

add_library(cara_leargas STATIC input_ring.c)

if(CARA_TARGET STREQUAL "riscv64")
    target_link_libraries(cara_leargas
        PUBLIC  cara_headers
        PRIVATE cara_kernel_flags
    )
else()
    target_link_libraries(cara_leargas
        PUBLIC  cara_headers
        PRIVATE cara_flags
    )
endif()
