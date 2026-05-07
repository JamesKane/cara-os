// SPDX-License-Identifier: BSD-2-Clause
//
// Populate struct CroiPlatform from the FDT. Phase 1 slice 1 only needs
// the console UART; everything else is left zeroed and will be filled in
// as later slices grow into it.

#include <cara/fdt.h>
#include <cara/platform.h>
#include <cara/types.h>

// Strip an options suffix ("foo:115200n8") from a stdout-path entry by
// returning the position of the ':' or the end of the string.
static u32 path_len_no_options(const char *p)
{
    u32 i = 0;
    while (p[i] != 0 && p[i] != ':') {
        i++;
    }
    return i;
}

static int resolve_stdout(const struct Fdt *fdt, u32 *uart_off_out)
{
    u32 chosen = 0;
    int rc = Fdt_ResolvePath(fdt, "/chosen", &chosen);
    if (rc != CARA_EOK) {
        return rc;
    }
    const char *path = Fdt_PropStr(fdt, chosen, "stdout-path");
    if (!path) {
        return CARA_ENOTFOUND;
    }

    // Some firmwares prefix with an alias name ("serial0:115200n8") rather
    // than a full path. If the string begins with '/', treat it as a path;
    // otherwise look it up through /aliases.
    if (path[0] == '/') {
        u32 n = path_len_no_options(path);
        if (n == 0 || n >= 256) {
            return CARA_EINVAL;
        }
        char tmp[256];
        for (u32 i = 0; i < n; i++) {
            tmp[i] = path[i];
        }
        tmp[n] = 0;
        return Fdt_ResolvePath(fdt, tmp, uart_off_out);
    }

    // Alias lookup.
    u32 aliases = 0;
    rc = Fdt_ResolvePath(fdt, "/aliases", &aliases);
    if (rc != CARA_EOK) {
        return rc;
    }
    u32 n = path_len_no_options(path);
    if (n == 0 || n >= 64) {
        return CARA_EINVAL;
    }
    char alias[64];
    for (u32 i = 0; i < n; i++) {
        alias[i] = path[i];
    }
    alias[n] = 0;

    const char *target = Fdt_PropStr(fdt, aliases, alias);
    if (!target) {
        return CARA_ENOTFOUND;
    }
    return Fdt_ResolvePath(fdt, target, uart_off_out);
}

[[nodiscard]] int Platform_FromFdt(struct CroiPlatform *out, const struct Fdt *fdt)
{
    if (!out || !fdt) {
        return CARA_EINVAL;
    }
    out->console.kind = CROI_UART_NONE;
    out->console.base = 0;
    out->console.irq = 0;
    out->console.reg_shift = 0;
    out->console.reg_io_width = 1;
    out->console.clock_hz = 0;
    out->console.baud = 115200;

    u32 uart = 0;
    int rc = resolve_stdout(fdt, &uart);
    if (rc != CARA_EOK) {
        // Fallback: try first ns16550-class node we can find.
        u32 nx = 0;
        if (Fdt_FindByCompatible(fdt, "ns16550a", &nx) == CARA_EOK) {
            uart = nx;
        } else {
            nx = 0;
            if (Fdt_FindByCompatible(fdt, "ns16550", &nx) == CARA_EOK) {
                uart = nx;
            } else {
                return rc;
            }
        }
    }

    u64 base = 0;
    u64 size = 0;
    rc = Fdt_PropReg(fdt, uart, 0, &base, &size);
    if (rc != CARA_EOK) {
        return rc;
    }

    u32 v32 = 0;
    out->console.kind = CROI_UART_NS16550;
    out->console.base = base;

    if (Fdt_PropU32(fdt, uart, "reg-shift", &v32) == CARA_EOK) {
        out->console.reg_shift = v32;
    }
    if (Fdt_PropU32(fdt, uart, "reg-io-width", &v32) == CARA_EOK) {
        out->console.reg_io_width = v32;
    }
    if (Fdt_PropU32(fdt, uart, "clock-frequency", &v32) == CARA_EOK) {
        out->console.clock_hz = v32;
    }
    if (Fdt_PropU32(fdt, uart, "current-speed", &v32) == CARA_EOK) {
        out->console.baud = v32;
    }
    if (Fdt_PropU32(fdt, uart, "interrupts", &v32) == CARA_EOK) {
        out->console.irq = v32;
    }

    return CARA_EOK;
}
