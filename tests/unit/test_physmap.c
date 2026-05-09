// SPDX-License-Identifier: BSD-2-Clause
//
// Mm_PhysMapFromFdt against the captured QEMU virt DTB. Asserts the
// known bank shape (one bank @ 0x80000000 size 0x8000000) and that a
// synthetic kernel range carved out of it produces an even smaller
// usable run.

#include <cara/fdt.h>
#include <cara/mm.h>
#include <cara/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CARA_TEST_DATADIR
#error "CARA_TEST_DATADIR must be defined"
#endif

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_physmap: FAIL: %s\n", msg);
    return code;
}

static u8 *load_file(const char *path, u32 *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return nullptr;
    }
    void *buf = aligned_alloc(8, ((size_t)n + 7) & ~(size_t)7);
    if (!buf) {
        fclose(f);
        return nullptr;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        return nullptr;
    }
    *size_out = (u32)n;
    return (u8 *)buf;
}

int main(void)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/qemu-virt.dtb", CARA_TEST_DATADIR);
    u32 size = 0;
    u8 *blob = load_file(path, &size);
    if (!blob) {
        fprintf(stderr, "test_physmap: cannot open %s\n", path);
        return 1;
    }

    struct Fdt fdt;
    if (Fdt_Open(&fdt, blob) != CARA_EOK) {
        return fail("Fdt_Open failed", 2);
    }

    // 1. No carve-outs: usable should equal the raw bank, page-aligned.
    struct PhysMap pm;
    if (Mm_PhysMapFromFdt(&pm, &fdt, 0, 0, 0, 0) != CARA_EOK) {
        return fail("PhysMap with no carve-outs failed", 3);
    }
    if (pm.n_banks != 1) {
        return fail("expected exactly one /memory bank on QEMU virt", 4);
    }
    if (pm.bank[0].base != 0x80000000ull || pm.bank[0].size != 0x8000000ull) {
        return fail("bank 0 shape wrong", 5);
    }
    if (pm.n_usable != 1) {
        return fail("expected one usable run with no carve-outs", 6);
    }
    if (pm.usable[0].base != 0x80000000ull || pm.usable[0].size != 0x8000000ull) {
        return fail("usable run not equal to bank without carve-outs", 7);
    }

    // 2. Kernel range carve-out: kernel @ 0x80200000 size 0x10000.
    if (Mm_PhysMapFromFdt(&pm, &fdt, 0x80200000ull, 0x80210000ull, 0, 0) != CARA_EOK) {
        return fail("PhysMap with kernel carve-out failed", 8);
    }
    // Now we expect TWO usable runs: 0x80000000..0x80200000 and 0x80210000..0x88000000.
    if (pm.n_usable != 2) {
        return fail("expected 2 usable runs around kernel carve-out", 9);
    }
    if (pm.usable[0].base != 0x80000000ull || pm.usable[0].size != 0x00200000ull) {
        return fail("low usable run wrong after kernel carve-out", 10);
    }
    if (pm.usable[1].base != 0x80210000ull || pm.usable[1].size != 0x07DF0000ull) {
        return fail("high usable run wrong after kernel carve-out", 11);
    }
    if (pm.usable_bytes != 0x00200000ull + 0x07DF0000ull) {
        return fail("usable_bytes mismatch", 12);
    }

    // 3. Misaligned carve-out: still produces page-aligned usable runs.
    if (Mm_PhysMapFromFdt(&pm, &fdt, 0x80200123ull, 0x80210FFFull, // misaligned ends
                          0, 0) != CARA_EOK) {
        return fail("PhysMap with misaligned carve-out failed", 13);
    }
    for (u32 i = 0; i < pm.n_usable; i++) {
        if ((pm.usable[i].base & 0xFFFull) != 0 || (pm.usable[i].size & 0xFFFull) != 0) {
            return fail("usable run not page-aligned after misaligned carve", 14);
        }
    }

    free(blob);
    puts("physmap ok");
    return 0;
}
