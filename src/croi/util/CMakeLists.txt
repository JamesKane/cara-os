# SPDX-License-Identifier: BSD-2-Clause
#
# cara_util: small kernel-internal helpers that don't fit elsewhere
# (TagItem walker for now; will grow as Phase C's MakeLibrary path
# needs it).

add_library(cara_util STATIC
    tagitem.c
)
target_link_libraries(cara_util
    PUBLIC cara_headers
    PRIVATE cara_kernel_flags
)
