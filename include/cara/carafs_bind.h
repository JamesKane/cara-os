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

// (The F6/G3 thin filesystem syscall backends Croi_Fs_Read/Write_Impl
// were the Phase-2 stopgap for Clar's drawer note; retired in L3.7 once
// dos.library Open/Read/Write/Close owns app-facing file I/O.)

// ---- Boot startup-sequence runner (F6/G4, docs/LOGAIC_BOOT.md §5) ------------
//
// Read S/Startup-Sequence from the root mount and run its commands (the
// AmigaDOS boot idiom, minimal): `; …` comments, `Echo <text>` (logs),
// and `LoadWB` (request the Workbench). Returns true when the Workbench
// should launch — also the default when there is no sequence. A fresh
// volume is seeded with a default sequence at format (Croi_Carafs_BringUp).
[[nodiscard]] bool Croi_Boot_RunStartup(void);

#endif
