# SPDX-License-Identifier: BSD-2-Clause
#
# cara_gpt: cleanroom GUID Partition Table parse/format (Phase 2
# Subgoal 3, docs/LOGAIC_BOOT.md). Portable pure logic behind the
# GptDev seam — host unit tests bind it to memory, the kernel binds it
# to NVMe. Logaic owns the boot path, so it lives here.

add_library(cara_gpt STATIC
    gpt.c
)

if(CARA_TARGET STREQUAL "riscv64")
    target_link_libraries(cara_gpt PUBLIC cara_headers PRIVATE cara_kernel_flags)
else()
    target_link_libraries(cara_gpt PUBLIC cara_headers PRIVATE cara_flags)
endif()
