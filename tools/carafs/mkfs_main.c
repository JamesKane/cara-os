// SPDX-License-Identifier: BSD-2-Clause
//
// mkfs.carafs — create a CaraFS volume in an image file (or grow one
// to size first). Hosted tool; the format work is all done by the
// shared core (Carafs_Mkfs), so what this file owns is argv parsing,
// file creation, randomness, and the clock.
//
//   mkfs.carafs <image> [-s <MiB>] [-b <block-size>] [-n <name>]

#include <cara/carafs.h>
#include <cara/types.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "filebdev.h"

static u64 now_carafs_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    u64 unix_ns = (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
    return unix_ns - CARAFS_EPOCH_UNIX_OFFSET_S * 1000000000ull;
}

int main(int argc, char **argv)
{
    const char *path = nullptr;
    const char *name = "Work";
    u64 size_mib = 0;
    u32 block_size = 1u << CARAFS_DEF_BLOCK_LOG2;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            size_mib = strtoull(argv[++i], nullptr, 0);
        } else if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            block_size = (u32)strtoul(argv[++i], nullptr, 0);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            name = argv[++i];
        } else if (argv[i][0] != '-' && !path) {
            path = argv[i];
        } else {
            fprintf(stderr, "usage: mkfs.carafs <image> [-s MiB] [-b block-size] [-n name]\n");
            return 2;
        }
    }
    if (!path) {
        fprintf(stderr, "mkfs.carafs: no image file given\n");
        return 2;
    }

    // Create / size the image when -s was given.
    if (size_mib) {
        int fd = open(path, O_RDWR | O_CREAT, 0644);
        if (fd < 0 || ftruncate(fd, (off_t)(size_mib << 20)) != 0) {
            perror(path);
            return 1;
        }
        close(fd);
    }

    struct FileBdev fb;
    struct CarafsBdev bdev;
    if (filebdev_open(&bdev, &fb, path, block_size, true) != CARA_EOK) {
        return 1;
    }

    u8 uuid[16];
    {
        FILE *r = fopen("/dev/urandom", "rb");
        if (!r || fread(uuid, 1, sizeof(uuid), r) != sizeof(uuid)) {
            fprintf(stderr, "mkfs.carafs: cannot read /dev/urandom\n");
            return 1;
        }
        fclose(r);
    }

    struct CarafsMkfsOpts opts = {
        .block_size_log2 = (u32)__builtin_ctz(block_size),
        .name = name,
        .name_len = (u32)strlen(name),
        .uuid = uuid,
        .now_ns = now_carafs_ns(),
    };
    void *scratch = malloc(block_size);
    int rc = Carafs_Mkfs(&bdev, &opts, scratch, block_size);
    free(scratch);
    close(fb.fd);
    if (rc != CARA_EOK) {
        fprintf(stderr, "mkfs.carafs: failed: %d\n", rc);
        return 1;
    }
    printf("mkfs.carafs: '%s' on %s: %llu blocks x %u B\n", name, path,
           (unsigned long long)bdev.n_blocks, block_size);
    return 0;
}
