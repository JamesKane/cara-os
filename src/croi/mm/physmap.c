// SPDX-License-Identifier: BSD-2-Clause
//
// Build a Cara physical-memory map from the FDT. Walks /memory@* for
// raw banks, then subtracts (kernel image, DTB, /memreserve/,
// /reserved-memory/*) to produce a list of page-aligned usable runs
// the page allocator may hand out.

#include <cara/fdt.h>
#include <cara/mm.h>
#include <cara/types.h>

#define PAGE_SIZE 4096ull
#define MAX_RESERVED 32

struct ReservedList {
    struct PhysRun runs[MAX_RESERVED];
    u32 n;
};

static int reserved_add(struct ReservedList *rl, u64 base, u64 size)
{
    if (size == 0) {
        return CARA_EOK;
    }
    if (rl->n >= MAX_RESERVED) {
        return CARA_ERANGE;
    }
    rl->runs[rl->n].base = base;
    rl->runs[rl->n].size = size;
    rl->n++;
    return CARA_EOK;
}

// Subtract one reserved interval from one usable run, possibly
// splitting into head + tail. out has space for two PhysRun.
static u32 subtract_one(struct PhysRun bank, struct PhysRun rsv, struct PhysRun *out)
{
    u64 b_lo = bank.base;
    u64 b_hi = bank.base + bank.size;
    u64 r_lo = rsv.base;
    u64 r_hi = rsv.base + rsv.size;

    if (r_hi <= b_lo || r_lo >= b_hi) {
        out[0] = bank;
        return 1;
    }
    u32 n = 0;
    if (r_lo > b_lo) {
        out[n].base = b_lo;
        out[n].size = r_lo - b_lo;
        n++;
    }
    if (r_hi < b_hi) {
        out[n].base = r_hi;
        out[n].size = b_hi - r_hi;
        n++;
    }
    return n;
}

static u64 align_up(u64 v, u64 a)
{
    return (v + a - 1) & ~(a - 1);
}

static u64 align_down(u64 v, u64 a)
{
    return v & ~(a - 1);
}

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

[[nodiscard]] int Mm_PhysMapFromFdt(struct PhysMap *out, const struct Fdt *fdt,
                                    u64 kernel_phys_start, u64 kernel_phys_end, u64 dtb_phys_start,
                                    u64 dtb_phys_end)
{
    if (!out || !fdt) {
        return CARA_EINVAL;
    }
    *out = (struct PhysMap){};

    // ---- /memory@* banks ----
    u32 root = Fdt_Root(fdt);
    u32 cur = 0, child = 0;
    while (Fdt_ChildIter(fdt, root, &cur, &child) == CARA_EOK) {
        const char *dtype = Fdt_PropStr(fdt, child, "device_type");
        if (!dtype || !str_eq(dtype, "memory")) {
            continue;
        }
        u32 idx = 0;
        u64 base = 0, size = 0;
        while (Fdt_PropReg(fdt, child, idx, &base, &size) == CARA_EOK) {
            if (out->n_banks >= CARA_MAX_PHYS_BANKS) {
                return CARA_ERANGE;
            }
            out->bank[out->n_banks].base = base;
            out->bank[out->n_banks].size = size;
            out->n_banks++;
            out->total_bytes += size;
            idx++;
        }
    }
    if (out->n_banks == 0) {
        return CARA_ENOTFOUND;
    }

    // ---- collect reserved regions ----
    struct ReservedList rl = { 0 };

    u32 rcur = 0;
    u64 rb = 0, rs = 0;
    while (Fdt_RsvIter(fdt, &rcur, &rb, &rs) == CARA_EOK) {
        int rc = reserved_add(&rl, rb, rs);
        if (rc != CARA_EOK) {
            return rc;
        }
    }

    u32 reserved_node = 0;
    if (Fdt_ResolvePath(fdt, "/reserved-memory", &reserved_node) == CARA_EOK) {
        cur = 0;
        u32 rsv_child = 0;
        while (Fdt_ChildIter(fdt, reserved_node, &cur, &rsv_child) == CARA_EOK) {
            u32 ridx = 0;
            while (Fdt_PropReg(fdt, rsv_child, ridx, &rb, &rs) == CARA_EOK) {
                int rc = reserved_add(&rl, rb, rs);
                if (rc != CARA_EOK) {
                    return rc;
                }
                ridx++;
            }
        }
    }

    if (kernel_phys_end > kernel_phys_start) {
        int rc = reserved_add(&rl, kernel_phys_start, kernel_phys_end - kernel_phys_start);
        if (rc != CARA_EOK) {
            return rc;
        }
    }
    if (dtb_phys_end > dtb_phys_start) {
        int rc = reserved_add(&rl, dtb_phys_start, dtb_phys_end - dtb_phys_start);
        if (rc != CARA_EOK) {
            return rc;
        }
    }

    // ---- subtract reserved from banks ----
    struct PhysRun work[CARA_MAX_PHYS_RUNS * 2];
    u32 n_work = 0;
    for (u32 i = 0; i < out->n_banks; i++) {
        if (n_work >= CARA_MAX_PHYS_RUNS * 2) {
            return CARA_ERANGE;
        }
        work[n_work++] = out->bank[i];
    }

    for (u32 r = 0; r < rl.n; r++) {
        struct PhysRun next[CARA_MAX_PHYS_RUNS * 2];
        u32 n_next = 0;
        for (u32 i = 0; i < n_work; i++) {
            struct PhysRun split[2];
            u32 n_split = subtract_one(work[i], rl.runs[r], split);
            for (u32 s = 0; s < n_split; s++) {
                if (n_next >= CARA_MAX_PHYS_RUNS * 2) {
                    return CARA_ERANGE;
                }
                next[n_next++] = split[s];
            }
        }
        n_work = n_next;
        for (u32 i = 0; i < n_work; i++) {
            work[i] = next[i];
        }
    }

    // ---- page-align and copy to output ----
    for (u32 i = 0; i < n_work; i++) {
        u64 lo = align_up(work[i].base, PAGE_SIZE);
        u64 hi = align_down(work[i].base + work[i].size, PAGE_SIZE);
        if (hi <= lo) {
            continue;
        }
        if (out->n_usable >= CARA_MAX_PHYS_RUNS) {
            return CARA_ERANGE;
        }
        out->usable[out->n_usable].base = lo;
        out->usable[out->n_usable].size = hi - lo;
        out->n_usable++;
        out->usable_bytes += hi - lo;
    }

    return CARA_EOK;
}
