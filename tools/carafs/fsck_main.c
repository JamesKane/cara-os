// SPDX-License-Identifier: BSD-2-Clause
//
// fsck.carafs — read-only structural check of a CaraFS image.
// Exit codes: 0 clean, 1 errors found, 2 unusable / bad invocation.
//
//   fsck.carafs <image> [-b <block-size>]

#include <cara/carafs.h>
#include <cara/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "filebdev.h"

int main(int argc, char **argv)
{
    const char *path = nullptr;
    u32 block_size = 1u << CARAFS_DEF_BLOCK_LOG2;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            block_size = (u32)strtoul(argv[++i], nullptr, 0);
        } else if (argv[i][0] != '-' && !path) {
            path = argv[i];
        } else {
            fprintf(stderr, "usage: fsck.carafs <image> [-b block-size]\n");
            return 2;
        }
    }
    if (!path) {
        fprintf(stderr, "fsck.carafs: no image file given\n");
        return 2;
    }

    struct FileBdev fb;
    struct CarafsBdev bdev;
    if (filebdev_open(&bdev, &fb, path, block_size, false) != CARA_EOK) {
        return 2;
    }

    void *scratch = malloc(2ull * block_size);
    struct CarafsFsckReport rep;
    int rc = Carafs_Fsck(&bdev, scratch, 2ull * block_size, &rep);
    free(scratch);
    close(fb.fd);

    if (rc != CARA_EOK) {
        fprintf(stderr, "fsck.carafs: cannot check: %d\n", rc);
        return 2;
    }
    if (rep.errors) {
        fprintf(stderr, "fsck.carafs: %u error(s), %u warning(s); first: %s (block %llu)\n",
                rep.errors, rep.warnings, rep.first_error,
                (unsigned long long)rep.first_error_block);
        return 1;
    }
    printf("fsck.carafs: clean (%llu blocks checked, %u warning(s))\n",
           (unsigned long long)rep.blocks_checked, rep.warnings);
    return 0;
}
