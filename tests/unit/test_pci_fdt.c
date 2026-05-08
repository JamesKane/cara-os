// SPDX-License-Identifier: BSD-2-Clause
//
// Host unit test for Croi_Pci_HostBridgeFromFdt against the captured
// QEMU virt DTB. Asserts the parser decodes the
// `pci-host-ecam-generic` host bridge node — ECAM base, bus-range,
// and the three `ranges` entries (I/O, MEM32, MEM64) — to the values
// observed when the DTB was captured.

#include <cara/fdt.h>
#include <cara/pci.h>
#include <cara/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CARA_TEST_DATADIR
#error "CARA_TEST_DATADIR must be defined to the absolute tests/data path."
#endif

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_pci_fdt: FAIL: %s\n", msg);
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
    return buf;
}

int main(void)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/qemu-virt.dtb", CARA_TEST_DATADIR);
    u32 size = 0;
    u8 *blob = load_file(path, &size);
    if (!blob) {
        return fail("could not load qemu-virt.dtb", 1);
    }

    (void)size;     // Fdt_Open trusts the FDT_BEGIN_NODE header
    struct Fdt fdt;
    int rc = Fdt_Open(&fdt, blob);
    if (rc != CARA_EOK) {
        free(blob);
        return fail("Fdt_Open rejected the DTB", 2);
    }

    struct PciHostBridge bridge;
    rc = Croi_Pci_HostBridgeFromFdt(&bridge, &fdt);
    if (rc != CARA_EOK) {
        free(blob);
        return fail("Croi_Pci_HostBridgeFromFdt failed", 3);
    }

    if (bridge.ecam_base != 0x30000000ull) {
        free(blob);
        return fail("ECAM base != 0x30000000", 4);
    }
    if (bridge.ecam_size != 0x10000000ull) {
        free(blob);
        return fail("ECAM size != 0x10000000 (256 MiB)", 5);
    }
    if (bridge.bus_first != 0 || bridge.bus_last != 0xFF) {
        free(blob);
        return fail("bus-range != [0..0xff]", 6);
    }
    if (bridge.n_ranges != 3) {
        free(blob);
        return fail("expected 3 ranges (I/O + MEM32 + MEM64)", 7);
    }

    // ranges[0] — I/O at CPU 0x03000000, 64 KiB.
    if (bridge.range[0].kind != PCI_RANGE_IO
        || bridge.range[0].cpu_addr != 0x03000000ull
        || bridge.range[0].size     != 0x00010000ull) {
        free(blob);
        return fail("ranges[0] != I/O 64 KiB @ 0x03000000", 8);
    }
    // ranges[1] — MEM32 at CPU 0x40000000, 1 GiB.
    if (bridge.range[1].kind != PCI_RANGE_MEM32
        || bridge.range[1].cpu_addr != 0x40000000ull
        || bridge.range[1].size     != 0x40000000ull) {
        free(blob);
        return fail("ranges[1] != MEM32 1 GiB @ 0x40000000", 9);
    }
    // ranges[2] — MEM64 at CPU 0x400000000, 16 GiB.
    if (bridge.range[2].kind != PCI_RANGE_MEM64
        || bridge.range[2].cpu_addr != 0x400000000ull
        || bridge.range[2].size     != 0x400000000ull) {
        free(blob);
        return fail("ranges[2] != MEM64 16 GiB @ 0x400000000", 10);
    }

    free(blob);
    puts("pci-fdt smoke ok");
    return 0;
}
