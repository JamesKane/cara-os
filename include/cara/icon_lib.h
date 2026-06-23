// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ icon.library LVOs Croi serves
// (docs/ICON.md). icon.library is `syscall` flavour: each implemented
// LVO reaches src/croi/syscall/syscall.c via its trampoline
// (Cara_Trampoline_Icon_* in src/croi/icon/trampolines.S), and the
// dispatcher routes the SYS_Icon_* to the Croi_Icon_*_Impl below. The
// impls run S-mode and (de)serialise an object's `cara.icon` xattr
// (Carafs_Xattr*) into a heap struct DiskObject. FindToolType /
// MatchToolValue / BumpRevision are `local` flavour and live in the RX
// page, not here.

#ifndef CARA_ICON_LIB_H
#define CARA_ICON_LIB_H

#include <cara/types.h>
#include <exec/types.h>
#include <workbench/workbench.h>

struct CarafsMount;
struct Library;
struct IconBase;

// ---- Reserved-slot library hooks (`local` flavour) ------------------
struct Library *Croi_Icon_Open(struct Library *base, struct IconBase *ib);
void Croi_Icon_Close(struct Library *base, struct IconBase *ib);
void Croi_Icon_Expunge(struct Library *base, struct IconBase *ib);
ULONG Croi_Icon_ExtFunc(struct Library *base, struct IconBase *ib);

// ---- LVO impls (L11.1) ----------------------------------------------
// GetDiskObject resolves `name` to its object and parses the cara.icon
// xattr; nullptr when the object or its xattr is absent/malformed.
// FreeDiskObject releases the single shared-heap allocation.
struct DiskObject *Croi_Icon_GetDiskObject_Impl(STRPTR name);
void Croi_Icon_FreeDiskObject_Impl(struct DiskObject *diskobj);

// ---- Internal seams (shared with the kernel test) -------------------
// Largest cara.icon blob handled inline (a default DiskObject is tens of
// bytes; bounds the parse/seed scratch buffers).
constexpr u32 CARA_ICON_BLOB_MAX = 2048;

// Read + parse the cara.icon xattr off `cnode` into a heap DiskObject
// (nullptr if absent/malformed). GetDiskObject calls this after path
// resolution; the kernel test drives it directly on a seeded cnode.
struct DiskObject *Croi_Icon_ReadCnode(struct CarafsMount *m, u64 cnode);

// Serialise a DiskObject into the cara.icon blob; returns the length, or
// 0 on overflow/error. buf holds up to CARA_ICON_BLOB_MAX bytes. Used to
// seed the test xattr; reused by PutDiskObject (L11.2).
u32 Croi_Icon_BlobBuild(const struct DiskObject *dobj, u8 *buf, u32 buflen);

#endif // CARA_ICON_LIB_H
