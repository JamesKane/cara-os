// SPDX-License-Identifier: BSD-2-Clause
//
// FDT parser: header validation. Hand-crafted malformed blobs probe each
// validation gate. None of the inputs are valid; every Fdt_Open call must
// return a specific negative error code.

#include <cara/fdt.h>
#include <cara/types.h>

#include <stdio.h>
#include <string.h>

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_fdt_header: FAIL: %s\n", msg);
    return code;
}

static void be32_put(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

// Build a minimal-but-syntactically-correct DTB header in `out`.
// Returns the totalsize written.
static u32 build_min_header(u8 *out, u32 magic, u32 version, u32 last_comp,
                            u32 totalsize, u32 off_struct, u32 size_struct,
                            u32 off_strings, u32 size_strings, u32 off_rsvmap)
{
    be32_put(out + 0, magic);
    be32_put(out + 4, totalsize);
    be32_put(out + 8, off_struct);
    be32_put(out + 12, off_strings);
    be32_put(out + 16, off_rsvmap);
    be32_put(out + 20, version);
    be32_put(out + 24, last_comp);
    be32_put(out + 28, 0);              // boot_cpuid_phys
    be32_put(out + 32, size_strings);
    be32_put(out + 36, size_struct);
    return totalsize;
}

int main(void)
{
    u8 blob[256];
    struct Fdt fdt;

    // 1. Bad magic.
    memset(blob, 0, sizeof(blob));
    build_min_header(blob, 0xDEADBEEF, 17, 16, 64, 40, 4, 56, 0, 40);
    if (Fdt_Open(&fdt, blob) != CARA_EBADMAGIC) {
        return fail("bad magic accepted", 1);
    }

    // 2. Version too old.
    memset(blob, 0, sizeof(blob));
    build_min_header(blob, 0xD00DFEED, 16, 16, 64, 40, 4, 56, 0, 40);
    if (Fdt_Open(&fdt, blob) != CARA_EBADVERSION) {
        return fail("version 16 accepted", 2);
    }

    // 3. last_comp_version too new.
    memset(blob, 0, sizeof(blob));
    build_min_header(blob, 0xD00DFEED, 17, 17, 64, 40, 4, 56, 0, 40);
    if (Fdt_Open(&fdt, blob) != CARA_EBADVERSION) {
        return fail("last_comp 17 accepted", 3);
    }

    // 4. totalsize smaller than header.
    memset(blob, 0, sizeof(blob));
    build_min_header(blob, 0xD00DFEED, 17, 16, 30, 40, 4, 56, 0, 40);
    if (Fdt_Open(&fdt, blob) != CARA_EINVAL) {
        return fail("undersized totalsize accepted", 4);
    }

    // 5. Struct extends past totalsize.
    memset(blob, 0, sizeof(blob));
    build_min_header(blob, 0xD00DFEED, 17, 16, 60, 40, 100, 60, 0, 40);
    if (Fdt_Open(&fdt, blob) != CARA_ERANGE) {
        return fail("struct overrun accepted", 5);
    }

    // 6. Strings extends past totalsize.
    memset(blob, 0, sizeof(blob));
    build_min_header(blob, 0xD00DFEED, 17, 16, 64, 40, 4, 60, 100, 40);
    if (Fdt_Open(&fdt, blob) != CARA_ERANGE) {
        return fail("strings overrun accepted", 6);
    }

    // 7. Struct offset misaligned.
    memset(blob, 0, sizeof(blob));
    build_min_header(blob, 0xD00DFEED, 17, 16, 80, 41, 4, 56, 0, 40);
    if (Fdt_Open(&fdt, blob) != CARA_ERANGE) {
        return fail("misaligned struct offset accepted", 7);
    }

    // 8. Reservation block misaligned.
    memset(blob, 0, sizeof(blob));
    build_min_header(blob, 0xD00DFEED, 17, 16, 80, 48, 4, 64, 0, 41);
    if (Fdt_Open(&fdt, blob) != CARA_ERANGE) {
        return fail("misaligned rsvmap accepted", 8);
    }

    // 9. Null pointer.
    if (Fdt_Open(&fdt, nullptr) != CARA_EINVAL) {
        return fail("null blob accepted", 9);
    }
    if (Fdt_Open(nullptr, blob) != CARA_EINVAL) {
        return fail("null out accepted", 10);
    }

    // 10. Misaligned blob pointer (DTB requires 8-byte alignment).
    u8 ext[300];
    memset(ext, 0, sizeof(ext));
    u8 *misaligned = ext + 1;
    build_min_header(misaligned, 0xD00DFEED, 17, 16, 64, 40, 4, 56, 0, 40);
    if (Fdt_Open(&fdt, misaligned) != CARA_EINVAL) {
        return fail("misaligned blob accepted", 11);
    }

    puts("fdt header validation ok");
    return 0;
}
