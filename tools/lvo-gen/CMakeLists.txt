# SPDX-License-Identifier: BSD-2-Clause
#
# lvo-gen — single-file C23 host tool. Consumes per-library `.conf`
# specs (per docs/LVO.md §4), emits the four artefact families
# (proto/<lib>.h inline stubs, <lib>/lvo.h constants, <owner>/<lib>_vec.c
# vec table + MakeLibrary tags, aistreoir/lvo_table.gen.h fragment).
# Built only on the host; the rv64 cross-build invokes the host binary
# at configure / build time via add_custom_command (B4).

add_executable(lvo-gen lvo-gen.c)
target_link_libraries(lvo-gen PRIVATE cara_flags)
