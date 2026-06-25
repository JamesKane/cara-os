# SPDX-License-Identifier: BSD-2-Clause
#
# cara_sched: cooperative kernel scheduler. Tier 2; later epics add
# preemption (timer-driven), per-task page tables (Epic J), and SMP
# secondary harts (Epic H).

add_library(cara_sched STATIC
    sched.c
)
# The RISC-V context switch (ctx_switch.S) + first-dispatch / U-mode entry
# (context.c) moved to src/croi/arch/riscv64/ in epic H (H.3); sched.c drives
# them via the arch_ctx_* seam (cara/arch.h). They link into croi alongside.
target_link_libraries(cara_sched
    PUBLIC cara_headers cara_kobj cara_loader
    PRIVATE cara_kernel_flags
)
