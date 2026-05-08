# SPDX-License-Identifier: BSD-2-Clause
#
# cara_sched: cooperative kernel scheduler. Tier 2; later epics add
# preemption (timer-driven), per-task page tables (Epic J), and SMP
# secondary harts (Epic H).

add_library(cara_sched STATIC
    sched.c
    ctx_switch.S
)
target_link_libraries(cara_sched
    PUBLIC cara_headers cara_kobj
    PRIVATE cara_kernel_flags
)
