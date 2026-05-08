# SPDX-License-Identifier: BSD-2-Clause

add_library(cara_syscall STATIC
    syscall.c
)
target_link_libraries(cara_syscall
    PUBLIC cara_headers cara_sched
    PRIVATE cara_kernel_flags
)
