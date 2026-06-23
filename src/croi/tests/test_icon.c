// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(icon_*): icon.library's DiskObject read path (L11.1)
// driven on a seeded cnode — no path resolution, so it runs from the
// kernel runner (which is not a Process). The by-name GetDiskObject
// (which Locks a path) is exercised by the Gleas round-trip in L11.2.
//
// We build a DiskObject, serialise it with Croi_Icon_BlobBuild, stash it
// as the `cara.icon` xattr of a fresh file via Carafs_XattrSet, then read
// it back with Croi_Icon_ReadCnode and assert every field — proving the
// blob codec + the xattr substrate end to end. Needs the boot-time
// CaraFS mount (same nvme-device caveat as the carafs_* tests).

#include <cara/carafs.h>
#include <cara/carafs_bind.h>
#include <cara/icon_lib.h>
#include <cara/test.h>
#include <cara/types.h>
#include <workbench/workbench.h>

static bool streq(const char *a, const char *b)
{
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

KERNEL_TEST(icon_diskobject_roundtrip)
{
    TEST_ASSERT(ctx, g_carafs_mounted, "CaraFS not mounted (no nvme device?)");
    u64 root = g_carafs.sb.root_cnode;

    // Idempotent across reboots: clear any leftover from a prior run.
    u64 f;
    if (Carafs_DirLookup(&g_carafs, root, "icontest", 8, &f, nullptr) == CARA_EOK) {
        TEST_ASSERT(ctx, Carafs_DirRemove(&g_carafs, root, "icontest", 8) == CARA_EOK,
                    "clear stale icontest");
    }
    TEST_ASSERT(ctx,
                Carafs_DirCreate(&g_carafs, root, "icontest", 8, CARAFS_T_FILE, &f) == CARA_EOK,
                "create icontest");

    // Build a WBTOOL DiskObject with two tool types + a default tool.
    static char *tt[] = { (char *)"CX_PRIORITY=0", (char *)"DONOTWAIT", nullptr };
    struct DiskObject d = { 0 };
    d.do_Magic = WB_DISKMAGIC;
    d.do_Version = WB_DISKVERSION;
    d.do_Type = WBTOOL;
    d.do_CurrentX = 64;
    d.do_CurrentY = 48;
    d.do_StackSize = 8192;
    d.do_DefaultTool = (char *)"SYS:Utilities/MultiView";
    d.do_ToolTypes = tt;

    static u8 blob[CARA_ICON_BLOB_MAX];
    u32 n = Croi_Icon_BlobBuild(&d, blob, sizeof blob);
    TEST_ASSERT(ctx, n > 0, "blob build");

    TEST_ASSERT(ctx, Carafs_XattrSet(&g_carafs, f, "cara.icon", 9, blob, n) == CARA_EOK,
                "seed cara.icon xattr");

    struct DiskObject *r = Croi_Icon_ReadCnode(&g_carafs, f);
    TEST_ASSERT(ctx, r != nullptr, "read DiskObject");
    TEST_ASSERT(ctx, r->do_Magic == WB_DISKMAGIC && r->do_Version == WB_DISKVERSION, "magic");
    TEST_ASSERT(ctx, r->do_Type == WBTOOL, "type");
    TEST_ASSERT(ctx, r->do_CurrentX == 64 && r->do_CurrentY == 48, "position");
    TEST_ASSERT(ctx, r->do_StackSize == 8192, "stack size");
    TEST_ASSERT(ctx, streq(r->do_DefaultTool, "SYS:Utilities/MultiView"), "default tool");
    TEST_ASSERT(ctx, r->do_ToolTypes != nullptr, "tool types present");
    TEST_ASSERT(ctx, streq(r->do_ToolTypes[0], "CX_PRIORITY=0"), "tool type 0");
    TEST_ASSERT(ctx, streq(r->do_ToolTypes[1], "DONOTWAIT"), "tool type 1");
    TEST_ASSERT(ctx, r->do_ToolTypes[2] == nullptr, "tool types NULL-terminated");
    TEST_ASSERT(ctx, r->do_DrawerData == nullptr, "no drawer data for a tool");
    Croi_Icon_FreeDiskObject_Impl(r);

    // A cnode with no cara.icon xattr yields nullptr (not a crash).
    u64 bare;
    if (Carafs_DirLookup(&g_carafs, root, "iconbare", 8, &bare, nullptr) == CARA_EOK) {
        TEST_ASSERT(ctx, Carafs_DirRemove(&g_carafs, root, "iconbare", 8) == CARA_EOK,
                    "clear stale iconbare");
    }
    TEST_ASSERT(ctx,
                Carafs_DirCreate(&g_carafs, root, "iconbare", 8, CARAFS_T_FILE, &bare) == CARA_EOK,
                "create iconbare");
    TEST_ASSERT(ctx, Croi_Icon_ReadCnode(&g_carafs, bare) == nullptr, "no xattr → nullptr");

    // Clean up so the next boot starts fresh.
    TEST_ASSERT(ctx, Carafs_DirRemove(&g_carafs, root, "icontest", 8) == CARA_EOK,
                "remove icontest");
    TEST_ASSERT(ctx, Carafs_DirRemove(&g_carafs, root, "iconbare", 8) == CARA_EOK,
                "remove iconbare");
}
