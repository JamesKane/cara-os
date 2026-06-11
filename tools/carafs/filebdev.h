// SPDX-License-Identifier: BSD-2-Clause
//
// File-backed CarafsBdev for the host tools (mkfs.carafs /
// fsck.carafs). Header-only; hosted-build only — the kernel binds
// the core to Croi_Nvme_* instead.

#ifndef CARAFS_TOOLS_FILEBDEV_H
#define CARAFS_TOOLS_FILEBDEV_H

#include <cara/carafs.h>
#include <cara/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct FileBdev {
    int fd;
    u32 block_size;
};

static int filebdev_read(void *ctx, u64 block, u32 n, void *buf)
{
    struct FileBdev *f = ctx;
    usize len = (usize)n * f->block_size;
    ssize_t got = pread(f->fd, buf, len, (off_t)(block * f->block_size));
    return got == (ssize_t)len ? CARA_EOK : CARA_EIO;
}

static int filebdev_write(void *ctx, u64 block, u32 n, const void *buf)
{
    struct FileBdev *f = ctx;
    usize len = (usize)n * f->block_size;
    ssize_t put = pwrite(f->fd, buf, len, (off_t)(block * f->block_size));
    return put == (ssize_t)len ? CARA_EOK : CARA_EIO;
}

static int filebdev_flush(void *ctx)
{
    struct FileBdev *f = ctx;
    return fsync(f->fd) == 0 ? CARA_EOK : CARA_EIO;
}

// Open `path` and present it as a bdev of `block_size`-sized blocks.
// The file size must already be a block multiple (mkfs sizes it).
static int filebdev_open(struct CarafsBdev *out, struct FileBdev *state, const char *path,
                         u32 block_size, bool writable)
{
    state->fd = open(path, writable ? O_RDWR : O_RDONLY);
    if (state->fd < 0) {
        fprintf(stderr, "open %s: %s\n", path, strerror(errno));
        return CARA_EIO;
    }
    state->block_size = block_size;
    off_t size = lseek(state->fd, 0, SEEK_END);
    if (size <= 0 || (u64)size % block_size != 0) {
        fprintf(stderr, "%s: size %lld is not a multiple of block size %u\n", path, (long long)size,
                block_size);
        close(state->fd);
        return CARA_EINVAL;
    }
    *out = (struct CarafsBdev){
        .ctx = state,
        .block_size = block_size,
        .n_blocks = (u64)size / block_size,
        .read = filebdev_read,
        .write = filebdev_write,
        .flush = filebdev_flush,
    };
    return CARA_EOK;
}

#endif
