# SPDX-License-Identifier: BSD-2-Clause
#
# Hosted CaraFS tools (ROADMAP Phase 2 Subgoal 4): mkfs.carafs and
# fsck.carafs. All format logic lives in the shared cara_carafs core;
# these are argv + file-I/O shells around it.

add_executable(mkfs.carafs mkfs_main.c)
target_link_libraries(mkfs.carafs PRIVATE cara_flags cara_headers cara_carafs)

add_executable(fsck.carafs fsck_main.c)
target_link_libraries(fsck.carafs PRIVATE cara_flags cara_headers cara_carafs)
