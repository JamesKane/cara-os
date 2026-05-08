# SPDX-License-Identifier: BSD-2-Clause
#
# cara_ipc: kernel IPC primitives (MsgPort wrapping Ring + Signal).

add_library(cara_ipc STATIC
    msgport.c
)
target_link_libraries(cara_ipc
    PUBLIC cara_headers cara_kobj cara_sched
    PRIVATE cara_kernel_flags
)
