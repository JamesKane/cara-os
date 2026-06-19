# SPDX-License-Identifier: BSD-2-Clause
#
# cara_carafs: the CaraFS core (docs/CARAFS.md §4). Portable pure
# logic behind the CarafsBdev seam — the same sources link into the
# kernel (mount over Croi_Nvme_*) and into host unit tests + the
# mkfs/fsck tools (file-backed bdev). Logaic owns it: this is the
# substrate dos.library's volumes/locks/files sit on in Phase 3.

add_library(cara_carafs STATIC
    format.c
    bdev.c
    cache.c
    mkfs.c
    fsck.c
    mount.c
    alloc.c
    cnode.c
    btree.c
    file.c
    dir.c
    journal.c
)

if(CARA_TARGET STREQUAL "riscv64")
    target_link_libraries(cara_carafs
        PUBLIC  cara_headers
        PRIVATE cara_kernel_flags
    )
else()
    target_link_libraries(cara_carafs
        PUBLIC  cara_headers
        PRIVATE cara_flags
    )
endif()
