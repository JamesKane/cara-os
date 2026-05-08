// SPDX-License-Identifier: BSD-2-Clause
//
// exec.library shared region helpers — bridge the linker symbols
// (in croi.lds `.exec_lib`) to the two views the kernel uses:
// the upper-half direct-map "kernel view" (writable from S-mode)
// and the user-VA 0x4000_0000+ view (mapped into every user task's
// PT for reading the vec table and executing the trampolines).

#include <cara/exec_lib_image.h>
#include <cara/log.h>
#include <cara/mm.h>
#include <cara/types.h>
#include <exec/execbase.h>
#include <exec/libraries.h>

// Positive-side reservation in src/croi/croi.lds (`.exec_lib`):
//     __exec_lib_base       0x4000_0800
//     __exec_lib_pos_end    0x4000_1800
// Total positive side = 0x1000 (4 KiB). struct Library lives at the
// start; the rest is library-private state for ExecBase to use.
#define CARA_EXEC_LIB_POS_SIZE 0x1000ull

struct Library *Croi_ExecLib_KernelView(void)
{
    u64 lma_base = _exec_lib_image_lma_start
                   + CARA_EXEC_LIB_USER_BASE_OFFSET;
    return (struct Library *)Mm_PhysToVirt(lma_base);
}

usize Croi_ExecLib_PrivateSize(void)
{
    // The library's private state past struct Library is enough to
    // hold a full struct ExecBase (which embeds struct Library at
    // offset 0). Phase C v0 leaves the ExecBase fields zero-init;
    // Phase 3+ wires them up incrementally (ThisTask per hart,
    // LibList mirroring the registry, etc.).
    static_assert(sizeof(struct ExecBase) <= CARA_EXEC_LIB_POS_SIZE,
                  "struct ExecBase exceeds the linker reservation in "
                  "src/croi/croi.lds (.exec_lib pos-side); bump the "
                  "reservation or trim the V36+ field set.");
    return (usize)(sizeof(struct ExecBase) - sizeof(struct Library));
}

[[nodiscard]] int Croi_ExecLib_InstallMapping(struct PageTable *pt)
{
    if (!pt) {
        return CARA_EINVAL;
    }
    u64 lma_start = _exec_lib_image_lma_start;
    u64 size      = _exec_lib_image_lma_size;

    if ((lma_start & (CARA_PAGE_SIZE - 1)) != 0
        || (size & (CARA_PAGE_SIZE - 1)) != 0) {
        LOG_FATAL("execli", "exec.library LMA range not page-aligned "
                            "(start=0x%llx size=0x%llx)",
                  lma_start, size);
        return CARA_EINVAL;
    }

    // R+W+X+U so the same VA serves three needs:
    //   - user mode reads the vec table and executes trampolines
    //   - kernel mode (running with the user task's PT during a
    //     syscall) writes lib_OpenCnt via the same address
    //   - kernel mode at boot (with the boot PT, which we also
    //     install this mapping into) writes the initial vec table
    //     and struct Library fields during Croi_MakeLibrary.
    // V0 hardening tradeoff: a malicious user could rewrite vec
    // entries to redirect calls within its own address space, but
    // can't escalate privilege — every call still ecalls into the
    // kernel and the kernel doesn't trust the vec table itself.
    u64 va = CARA_EXEC_LIB_USER_VA;
    for (u64 off = 0; off < size; off += CARA_PAGE_SIZE) {
        int rc = Page_Map(pt, va + off, lma_start + off, PTE_USER_RWX);
        if (rc != CARA_EOK) {
            LOG_FATAL("execli",
                      "Page_Map(0x%llx -> 0x%llx) failed: %d",
                      va + off, lma_start + off, rc);
            return rc;
        }
    }
    return CARA_EOK;
}

[[nodiscard]] int Croi_ExecLib_InstallInBootPT(void)
{
    // Read SATP to find the current (boot) PT's root, wrap it in a
    // PageTable struct, and install the same mapping there. After
    // this, kmain (which runs with the boot PT) can read+write
    // 0x4000_0000+ — Croi_MakeLibrary uses this view to populate
    // struct Library and the vec table.
    u64 satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    u64 ppn = satp & ((1ull << 44) - 1);
    u64 *root = (u64 *)Mm_PhysToVirt(ppn << 12);

    struct PageTable boot_pt = {
        .root = root,
        .asid = 0,
    };
    return Croi_ExecLib_InstallMapping(&boot_pt);
}
