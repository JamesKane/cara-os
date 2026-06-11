// SPDX-License-Identifier: BSD-2-Clause
//
// Memory-backed CarafsBdev + the shared geometry computation (F1).

#include <cara/carafs.h>
#include <cara/types.h>

#include "internal.h"

// ---- Memory bdev ------------------------------------------------------------

static int mem_read(void *ctx, u64 block, u32 n, void *buf)
{
    struct CarafsMemBdev *s = ctx;
    if (block + n > s->n_blocks) {
        return CARA_ERANGE;
    }
    memcpy(buf, s->mem + block * s->block_size, (usize)n * s->block_size);
    return CARA_EOK;
}

static int mem_write(void *ctx, u64 block, u32 n, const void *buf)
{
    struct CarafsMemBdev *s = ctx;
    if (block + n > s->n_blocks) {
        return CARA_ERANGE;
    }
    memcpy(s->mem + block * s->block_size, buf, (usize)n * s->block_size);
    return CARA_EOK;
}

static int mem_flush(void *ctx)
{
    (void)ctx;
    return CARA_EOK;
}

void Carafs_MemBdev_Init(struct CarafsBdev *out, struct CarafsMemBdev *state, void *mem,
                         u32 block_size, u64 n_blocks)
{
    *state = (struct CarafsMemBdev){
        .mem = mem,
        .block_size = block_size,
        .n_blocks = n_blocks,
    };
    *out = (struct CarafsBdev){
        .ctx = state,
        .block_size = block_size,
        .n_blocks = n_blocks,
        .read = mem_read,
        .write = mem_write,
        .flush = mem_flush,
    };
}

// ---- Geometry ---------------------------------------------------------------

[[nodiscard]] int carafs_geometry(struct CarafsGeometry *g, u32 block_size, u64 total_blocks)
{
    *g = (struct CarafsGeometry){ 0 };
    g->block_size = block_size;
    g->total_blocks = total_blocks;

    // Journal log: clamp(total/64, 1 MiB, 64 MiB) in blocks, +1 JSB
    // (CARAFS.md §3.9).
    u64 jlog = total_blocks / 64;
    u64 jmin = (1ull << 20) / block_size;
    u64 jmax = (64ull << 20) / block_size;
    if (jlog < jmin) {
        jlog = jmin;
    }
    if (jlog > jmax) {
        jlog = jmax;
    }
    g->journal_start = 1;
    g->journal_blocks = (u32)(jlog + 1);

    g->ag_first = 1 + g->journal_blocks;
    g->ag_size = carafs_ag_size(block_size);
    g->backup_sb = total_blocks - 1;

    // Data region: [ag_first, backup_sb). Need the AG bitmap block,
    // the root cnode, and breathing room.
    if (total_blocks < g->ag_first + 8 + 1) {
        return CARA_ERANGE;
    }
    u64 data_blocks = g->backup_sb - g->ag_first;
    g->ag_count = (u32)((data_blocks + g->ag_size - 1) / g->ag_size);
    g->root_cnode = g->ag_first + 1;
    return CARA_EOK;
}
