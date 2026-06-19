// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel binding of the portable CaraFS core to the NVMe driver (F5).
// One global CarafsBdev maps filesystem blocks onto NSID 1 LBAs and
// bounces DMA through a page-aligned buffer; one global CarafsMount is
// brought up at boot once the I/O queue pair is live.

#ifndef CARA_CARAFS_BIND_H
#define CARA_CARAFS_BIND_H

#include <cara/carafs.h>
#include <cara/types.h>

// The boot-time CaraFS mount over NVMe NSID 1. Valid only when
// g_carafs_mounted is true.
extern struct CarafsMount g_carafs;
extern bool g_carafs_mounted;

// Bring CaraFS up on the NVMe controller `g_nvme`: build the bdev,
// mount the volume, and — when no valid superblock is present — format
// it first. Idempotent. Requires g_nvme.io_ready. Returns CARA_EOK on a
// live mount, or an error (controller not ready, mkfs/mount failure).
[[nodiscard]] int Croi_Carafs_BringUp(void);

#endif
