// SPDX-License-Identifier: BSD-2-Clause
//
// FDT parser: queries against the captured QEMU virt DTB. Validates the
// parser against a known-good real-world blob — the DTB QEMU 11.x emits
// for `qemu-system-riscv64 -machine virt`. Numbers come from observed
// QEMU defaults; if QEMU changes them we must regenerate the captured
// DTB and update the assertions in lock-step.

#include <cara/fdt.h>
#include <cara/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CARA_TEST_DATADIR
#error "CARA_TEST_DATADIR must be defined to the absolute tests/data path."
#endif

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_fdt_qemu: FAIL: %s\n", msg);
    return code;
}

// mmap-ish: read the whole file into a fresh aligned buffer.
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
        fprintf(stderr, "test_fdt_qemu: cannot open %s\n", path);
        return 1;
    }

    struct Fdt fdt;
    int rc = Fdt_Open(&fdt, blob);
    if (rc != CARA_EOK) {
        return fail("Fdt_Open failed on captured DTB", 2);
    }

    // 1. Root model is "riscv-virtio,qemu".
    u32 root = Fdt_Root(&fdt);
    const char *model = Fdt_PropStr(&fdt, root, "model");
    if (!model || strcmp(model, "riscv-virtio,qemu") != 0) {
        return fail("root /model wrong", 3);
    }

    // 2. /memory@80000000 — base 0x80000000, size 0x8000000 (128 MiB
    //    default). reg uses #address-cells=2 #size-cells=2 from root.
    u32 mem = 0;
    if (Fdt_ResolvePath(&fdt, "/memory@80000000", &mem) != CARA_EOK) {
        return fail("ResolvePath /memory@80000000 failed", 4);
    }
    u64 base = 0, msize = 0;
    if (Fdt_PropReg(&fdt, mem, 0, &base, &msize) != CARA_EOK) {
        return fail("PropReg on memory failed", 5);
    }
    if (base != 0x80000000ull || msize != 0x8000000ull) {
        fprintf(stderr, "  got base=0x%llx size=0x%llx\n", (unsigned long long)base,
                (unsigned long long)msize);
        return fail("memory reg wrong", 6);
    }

    // 3. /soc/clint@2000000 — base 0x2000000, size 0x10000.
    u32 clint = 0;
    if (Fdt_ResolvePath(&fdt, "/soc/clint@2000000", &clint) != CARA_EOK) {
        return fail("ResolvePath clint failed", 7);
    }
    if (Fdt_PropReg(&fdt, clint, 0, &base, &msize) != CARA_EOK || base != 0x2000000ull ||
        msize != 0x10000ull) {
        return fail("clint reg wrong", 8);
    }

    // 4. /soc/plic@c000000 — base 0xc000000, size 0x600000.
    u32 plic = 0;
    if (Fdt_ResolvePath(&fdt, "/soc/plic@c000000", &plic) != CARA_EOK) {
        return fail("ResolvePath plic failed", 9);
    }
    if (Fdt_PropReg(&fdt, plic, 0, &base, &msize) != CARA_EOK || base != 0xc000000ull ||
        msize != 0x600000ull) {
        return fail("plic reg wrong", 10);
    }

    // 5. /soc/serial@10000000 — NS16550 UART on QEMU virt.
    u32 uart = 0;
    if (Fdt_ResolvePath(&fdt, "/soc/serial@10000000", &uart) != CARA_EOK) {
        return fail("ResolvePath uart failed", 11);
    }
    if (Fdt_PropReg(&fdt, uart, 0, &base, &msize) != CARA_EOK || base != 0x10000000ull) {
        return fail("uart reg wrong", 12);
    }

    // 6. FindByCompatible on "ns16550a" — must locate the UART.
    u32 byc = 0;
    if (Fdt_FindByCompatible(&fdt, "ns16550a", &byc) != CARA_EOK) {
        return fail("FindByCompatible ns16550a failed", 13);
    }
    if (byc != uart) {
        return fail("FindByCompatible found a different node than ResolvePath", 14);
    }

    // 7. /chosen/stdout-path resolves the same UART when we follow it.
    u32 chosen = 0;
    if (Fdt_ResolvePath(&fdt, "/chosen", &chosen) != CARA_EOK) {
        return fail("ResolvePath /chosen failed", 15);
    }
    const char *stdout_path = Fdt_PropStr(&fdt, chosen, "stdout-path");
    if (!stdout_path) {
        return fail("/chosen/stdout-path missing", 16);
    }
    // stdout-path on QEMU is typically "/soc/serial@10000000".
    u32 sp = 0;
    if (Fdt_ResolvePath(&fdt, stdout_path, &sp) != CARA_EOK || sp != uart) {
        // Some QEMU versions append an options suffix like
        // "serial0:115200n8"; our parser does not strip those, so accept
        // either an exact path match or the alias resolution path.
        u32 aliases = 0;
        if (Fdt_ResolvePath(&fdt, "/aliases", &aliases) != CARA_EOK) {
            return fail("/aliases missing", 17);
        }
        const char *serial0 = Fdt_PropStr(&fdt, aliases, "serial0");
        if (!serial0) {
            return fail("aliases/serial0 missing", 18);
        }
        if (Fdt_ResolvePath(&fdt, serial0, &sp) != CARA_EOK || sp != uart) {
            return fail("alias serial0 does not resolve to UART", 19);
        }
    }

    // 8. /cpus has at least one CPU, and at least one cpu node has a
    //    `riscv,isa` containing "rv64".
    u32 cpus = 0;
    if (Fdt_ResolvePath(&fdt, "/cpus", &cpus) != CARA_EOK) {
        return fail("ResolvePath /cpus failed", 20);
    }
    u32 cur = 0, ch = 0;
    bool found_rv64 = false;
    while (Fdt_ChildIter(&fdt, cpus, &cur, &ch) == CARA_EOK) {
        const char *isa = Fdt_PropStr(&fdt, ch, "riscv,isa");
        if (isa && strstr(isa, "rv64")) {
            found_rv64 = true;
            break;
        }
    }
    if (!found_rv64) {
        return fail("no /cpus child with rv64 isa", 21);
    }

    free(blob);
    puts("fdt qemu virt ok");
    return 0;
}
