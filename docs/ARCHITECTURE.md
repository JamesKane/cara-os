# CaraOS Architecture

A cleanroom C23 reimplementation of the AmigaOS Release 2 specification
(the 3rd Edition RKMs in `amigaos_kb_markdown/`, V36+). RISC-V 64 (Sv39,
S-mode) is the primary architecture; since epic H the ISA-specific kernel
internals (boot, trap, context switch, MMU, timer, firmware) sit behind a
small arch HAL (`src/croi/arch/<arch>/`, `cara/arch.h` — see
`docs/ARCH_HAL.md`) so a second architecture, **ARM64 (AArch64)**, can be
added without touching the portable kernel. One ISA per build, selected by
`CARA_ARCH`.

> Status: design draft. Implementation has not started. Numbers in tables (LVOs,
> address ranges, struct sizes) are *targets* for the first cut and will be
> revised as code lands. Anything marked **TBD** is an open question.

**Companion documents:**

- `docs/PRINCIPLES.md` — license (BSD-2-Clause), the no-third-party-deps
  rule, QEMU-first development, cleanroom commitments. Principles
  constrain architecture, not vice versa.
- `docs/ROADMAP.md` — the five development phases and their success
  criteria. This document describes *how the system is built*; ROADMAP
  describes *the order in which we build it*.
- `docs/HARDWARE_RV2.md` — concrete X1 / OrangePi RV2 register snapshot
  (derived, never imported into source).
- `docs/DTS_PARSER.md` — design of the FDT parser referenced from §9.
- `docs/LVO.md` — library / driver bridge (the `lvo-gen` model).
  Specifies the runtime mechanism §7 leaves abstract and answers
  the open questions §14.4 and (per-LVO) §14.7.

---

## 1. Goals and non-goals

### 1.1 Goals

1. **Source-level fidelity to AmigaOS Release 2 (V36+).** The
   contracts of Exec, AmigaDOS, Intuition, and graphics — message
   ports, libraries, LVO jumps, intrusive lists, signals, tasks,
   drawers, gadgets, screens, windows, RastPorts — plus the Release 2
   additions (`utility.library`, `gadtools.library`, `asl.library`,
   BOOPSI, `iffparse.library`, `commodities.library`, `icon.library`,
   `diskfont.library`) are reproduced as the verbatim AmigaOS V36+
   API surface: the libraries on disk are `exec.library`,
   `dos.library`, `intuition.library`, `graphics.library`,
   `utility.library`, `gadtools.library`, …; the public C symbols and
   struct names are spelled exactly as the 3rd Edition RKM autodocs
   print them; the LVO numbers match the canonical V36+ values. The
   contract is that a well-written AmigaOS V36+ source program builds
   for CaraOS without textual edits — see PRINCIPLES.md §3.1
   (brand-vs-API split). The CaraOS-branded names (Croi, Logaic,
   Leargas, Clar, Splanc, Guth, Bosca, Inntin, Gleas, Dath) name the
   *implementations* and the project, not the public API. The 3rd
   Edition RKMs in `amigaos_kb_markdown/` are the spec.
2. **Pointer-passing IPC ergonomics.** A producer hands a virtual pointer to a
   consumer and the consumer dereferences it. No marshalling, no serialization,
   no copy. This is what made the original system feel fast and what we keep.
3. **MMU-assisted stability.** A user `Gleas` (Tool) that faults must not take
   down the `Croi` kernel or other Gleasanna. Isolation is used for fault
   containment, not for security or multi-tenant separation.
4. **Cleanroom from the RKMs.** The markdown copies in `amigaos_kb_markdown/`
   are the spec we read; we never copy text or code from the original sources
   into CaraOS.
5. **Stock SoCs only.** Primary targets: StarFive **VisionFive 2** (JH7110)
   and **OrangePi RV2** (Spacemit K1), both RISC-V. No custom hardware, no
   custom chipset emulation; we stand on U-Boot, OpenSBI, and UEFI. The arch
   HAL (epic H, `docs/ARCH_HAL.md`) opens the door to a stock **ARM64** SoC
   as a second target — same "stand on the firmware (PSCI/UEFI)" rule.
6. **C23 throughout.** Use `typeof`, `constexpr`, `nullptr`, `[[nodiscard]]`,
   designated initializers, and `<stdatomic.h>` to retire the macro-heavy
   patterns of the original.

### 1.2 Non-goals

- **POSIX compatibility.** We are not a UNIX. `Logaic` will provide
  AmigaDOS-style filesystem semantics (volumes, assigns, locks), not
  open()/read()/write().
- **Multi-user separation.** There is one user, who is god. There is no uid,
  no acl, no capability ring beyond what the MMU enforces for crash isolation.
- **Source compatibility with original Amiga binaries.** We are RISC-V; the
  68k binary format, hunk loader, and ABI do not survive the port. (A
  hunk-to-CaraOS translator could exist as a future Gleas, but it is out of
  scope here.)
- **M-mode runtime.** We do not run in machine mode and we do not replace
  OpenSBI. Anything that needs M-mode privileges (timer programming, IPI,
  early console) goes through SBI calls.

---

## 2. Target platform

### 2.1 Hardware

| SoC                       | Cores       | RAM         | Notes                                  |
|---------------------------|-------------|-------------|----------------------------------------|
| Spacemit / Ky X1 (OrangePi RV2) | 8× Ky X60 | 4 GiB on the typical RV2 SKU | **Sole v0 target.** RVV in baseline ISA; Sstc; SMP exercised. |

The StarFive JH7110 (VisionFive 2) is **not** a v0 target. Its U74-MC cores
do not implement the V extension, so dual-targeting would force every
RVV-using path to ship a scalar fallback. We may revisit the JH7110 as a
port-out exercise once v0 is stable on RV2.

The X1 exposes RV64GCV at S-mode, an SBI v2.0 implementation in OpenSBI, a
UEFI boot environment via U-Boot, and a Device Tree Blob describing onboard
peripherals. Concrete X1 addresses (CLINT, PLIC, UARTs, memory banks) are
captured in `docs/HARDWARE_RV2.md`, derived from the linux-orangepi
`orange-pi-6.6-ky` DTS — but **no CaraOS code consumes those numbers as
constants**: device discovery is by FDT parser at boot (§9).

### 2.2 ISA assumptions

- **Base:** RV64GCV (IMAFDC + V) — required. The X1 advertises
  `rv64imafdcv` for every hart, so vector is in the *baseline* and Croi may
  use it on data-path code without conditional fallbacks.
- **Privileged:** Supervisor (S) and User (U) modes; M-mode reserved to
  OpenSBI.
- **Paging:** Sv39 in v0. Sv48 is a forward target once the address map and
  page-table walker are stable.
- **Timer:** **Sstc** is present (S-mode `stimecmp` is writable directly).
  Croi programs the next timer tick by writing `stimecmp` rather than
  paying an SBI ecall per interrupt.
- **Atomics:** the A extension; `<stdatomic.h>` lowers to `lr.w/sc.w` and
  `amoXXX.w/d` as needed. Ring buffer head/tail use plain
  `_Atomic uint32_t` — no fences beyond release/acquire pairing.
- **Vector:** RVV 1.0 (V). Croi saves/restores the V register file lazily
  via the `sstatus.VS` field (Initial → Clean → Dirty), the same pattern
  used for FP. Userspace Gleasanna may use V freely; `libcara` provides
  RVV-accelerated `memcpy`/`memset` variants selected at link time.
- **Floating point:** kernel is `-mabi=lp64` (no FP in S-mode); user Gleas
  use `lp64d`. Croi saves/restores the FP state lazily on context switch.
- **Other useful extensions on X1** (probed at boot, used opportunistically):
  Zicbom/Zicboz (cache management), Zicond (conditional ops),
  Zihintpause, Zfh/Zvfh (half-precision FP), Zba/Zbb/Zbc/Zbs (bitmanip),
  Zkt (constant-time crypto), Svinval, Svnapot (NAPOT page huge-page
  encoding), Svpbmt (page-based memory types).
- **Cache geometry on X1:** 64-byte L1 lines, 32 KiB I/D L1 per hart, 8
  cores in 2 clusters of 4. Cache-line padding in our shared atomics uses
  64 bytes (matches `riscv,cbom-block-size`).

### 2.3 Firmware stack

```
+----------------------------+   M-mode    fixed by SoC vendor
|  OpenSBI (fw_dynamic)      |
+----------------------------+
|  U-Boot                    |   S-mode    boots ESP, runs UEFI shim
+----------------------------+
|  splanc.efi                |   S-mode    CaraOS bootstrap (UEFI app)
+----------------------------+
|  Croi kernel               |   S-mode    handed control by splanc
+----------------------------+
|  Gleasanna (tasks)         |   U-mode    user code
+----------------------------+
```

We never replace anything below `splanc.efi`. Booting on different boards is a
matter of placing `splanc.efi` on the ESP at the path U-Boot's bootmgr will
discover (`/EFI/BOOT/BOOTRISCV64.EFI`).

---

## 3. Privilege model

### 3.1 Modes

- **M-mode:** OpenSBI. We call it via the `ecall` SBI ABI for: timer set
  (`sbi_set_timer`), inter-hart IPI, console put-char during early boot, and
  HSM (hart state management) for SMP startup.
- **S-mode:** Croi kernel. Owns `satp`, the trap vector, the timer, and the
  page tables.
- **U-mode:** every Gleas. A Gleas cannot touch CSRs, cannot disable
  interrupts, cannot reach kernel memory. It calls Croi via `ecall` (which
  traps to S-mode, not M-mode).

### 3.2 The "God-Mode" model

CaraOS is single-user. All Gleasanna run as the same logical principal.
There is no `setuid`, no capability ring, no permission check on whether
"this Gleas" is "allowed to" open `dos.library`. If you have a handle, you
can use it. If you don't, you can ask for one and you will get it.

What the MMU enforces is **address validity**, not **permission**:

- A wild pointer in a Gleas that lands outside the Gleas's accessible pages
  faults — and the fault is contained to that Gleas. Croi reaps it and prints
  a `Guru Meditation` to the console (the term survives, the bomb does not).
- A wild pointer in a Gleas that lands *inside* its own accessible pages but
  scribbles on its own heap is on you. We are not your mommy.

This is the trade we are making explicit: stability without paternalism.

---

## 4. Virtual memory: SASOS on Sv39

### 4.1 Why SASOS

Classic Exec passed messages by writing a pointer into a port and waking the
recipient. The recipient dereferenced the same pointer. This worked because
the address space was flat and shared.

We want that ergonomics back, on hardware with an MMU. The Single Address
Space OS pattern gives it to us: every virtual address means the same thing
to every task in the system, but the per-task page tables differ in *which
of those addresses are mapped accessible*. A pointer is therefore stable
across IPC boundaries; we just have to make sure the recipient's page table
has a mapping for the page the pointer lands in before they touch it.

### 4.2 Sv39 layout

Sv39 gives a 39-bit virtual space (512 GiB), sign-extended into the upper
bits of a 64-bit pointer. Canonical addresses are split into a low half
(user) and a high half (kernel):

```
0x0000_0000_0000_0000  +-----------------------------+
                       | Lower half: user-visible    |
                       | Single Address Space        |  256 GiB
                       |                             |
0x0000_003F_FFFF_FFFF  +-----------------------------+
                       | (non-canonical hole)        |
0xFFFF_FFC0_0000_0000  +-----------------------------+
                       | Upper half: kernel-only     |
                       | Croi private; identity map; |  256 GiB
                       | MMIO; per-hart stacks       |
0xFFFF_FFFF_FFFF_FFFF  +-----------------------------+
```

The lower half is shared across all tasks in the SASOS sense; the upper half
is mapped into *only* the kernel's view (in Sv39 there is one page table per
hart at a time, but the kernel's portion of every per-task table is shared
via shallow-copied top-level entries — see §4.4).

### 4.3 Lower-half regions

| Range (start)              | Size      | Purpose                                              | Default protection per task |
|----------------------------|-----------|------------------------------------------------------|-----------------------------|
| `0x0000_0000_0000_0000`    | 1 MiB     | Null guard, never mapped.                            | unmapped                    |
| `0x0000_0000_0010_0000`    | ~1 GiB    | Splanc transient image; reclaimed after boot.        | unmapped after handoff      |
| `0x0000_0000_4000_0000`    | 4 GiB     | **Shared library text/rodata.** `exec.library`, `dos.library`, `intuition.library`, `graphics.library`, `utility.library`, `gadtools.library`, … live here at fixed virtual addresses. | RX, all tasks |
| `0x0000_0001_0000_0000`    | 64 GiB    | **Shared system heap.** `AllocMem` (exec.library) allocates from here. Every allocation lives at a stable virtual address. | RW *only* on the owning task; no-access elsewhere. Tasks that need a peer's allocation receive an explicit map-grant via syscall. |
| `0x0000_0011_0000_0000`    | 176 GiB   | **Per-task private slabs.** Each Gleas owns a slab for its stack, BSS, and local heap. Each slab is at a stable virtual address chosen at task creation. | RW on owner; no-access on all others |
| `0x0000_003F_0000_0000`    | 4 GiB     | **IPC ring buffer pool.** Pages mapped pairwise into producer + consumer page tables. | RW on producer + consumer; no-access elsewhere |

Two consequences worth calling out:

1. The mapping `(virtual address) → (physical page)` is the same in every
   task's page table whenever the page is mapped at all. What differs is the
   permission bits (`R`, `W`, `X`, `U`) — including, crucially, the `V`
   (valid) bit. A page can be "no access" in another task's view by simply
   leaving its PTE invalid.
2. Allocators never have to revisit returned addresses. Once a virtual
   address has been handed out, that address means that data forever, until
   it is freed. This is what makes `PutMsg` zero-copy.

### 4.4 Upper-half (kernel)

| Range (start)              | Size     | Purpose |
|----------------------------|----------|---------|
| `0xFFFF_FFC0_0000_0000`    | 64 GiB   | Direct map of all physical RAM (kernel uses this for page table walks, DMA buffer setup, and any access to physical memory by physical address). |
| `0xFFFF_FFD0_0000_0000`    | 16 GiB   | Croi `.text`, `.rodata`, `.data`, `.bss`. |
| `0xFFFF_FFD4_0000_0000`    | 16 GiB   | Per-hart stacks and trap stacks. |
| `0xFFFF_FFE0_0000_0000`    | 64 GiB   | MMIO mapping window — peripherals enumerated from the DTB are mapped lazily here, RW, with `PMA=Device`. |

The kernel's top-level page-table entries (one per 1 GiB at the Sv39 root)
covering the upper half are shared across every per-task root: when Croi
touches kernel state, it walks pages that are mapped identically regardless
of which task last set `satp`.

### 4.5 ASID strategy

Each task is assigned an ASID on creation; `satp.ASID` is set on context
switch so that TLB entries from one task do not have to be flushed wholesale
when another runs. The ASID width on the X1's X60 cores is to be measured at
boot by writing all-ones to `satp.ASID`, reading back, and counting the
implemented bits. ASIDs recycle on overflow with a `sfence.vma x0, x0`
flush (the broad form, since `svinval` lets us amortise across the
post-overflow re-warm).

---

## 5. Croi kernel object model

### 5.1 Kernel objects (Kobj)

Everything Croi owns is a `Kobj`:

```c
typedef enum : uint16_t {
    KOBJ_NONE      = 0,
    KOBJ_TASK      = 1,
    KOBJ_MSGPORT   = 2,
    KOBJ_LIBRARY   = 3,
    KOBJ_SIGNAL    = 4,
    KOBJ_RING      = 5,
    KOBJ_MEMREGION = 6,
    KOBJ_DEVICE    = 7,
    KOBJ_VOLUME    = 8,    // Logaic
    KOBJ_LOCK      = 9,    // Logaic
    // ...
} KobjType;

struct Kobj {
    KobjType  type;
    uint16_t  flags;
    uint32_t  refcount;     // atomic
    uint64_t  id;           // monotonic, never reused
    // payload follows
};
```

Each Kobj is allocated from a slab keyed by type, lives in the upper half,
and is referenced from user space only via Handles.

### 5.2 Handle table

Each Task carries a per-task handle table:

```c
struct HandleSlot {
    struct Kobj *target;     // nullptr ⇒ free slot
    uint32_t     generation; // bumped on close; encoded into Handle
    uint32_t     reserved;   // (rights mask placeholder — unused in v0)
};

struct HandleTable {
    struct HandleSlot *slots;
    uint32_t           cap;
    uint32_t           free_head;  // free-list within slots[]
};
```

A `Handle` exposed to user space is a 32-bit value:

```
 31              16 15               0
+------------------+------------------+
| generation (16)  | slot index (16)  |
+------------------+------------------+
```

Public AmigaOS calls (`OpenLibrary`, `AllocMem`, `CreateMsgPort`, etc.)
return their canonical types — `struct Library *`, `void *`,
`struct MsgPort *` — and those pointers are valid in the calling task
because of SASOS. *Internally*, where the kernel needs a typed,
generation-checked reference (signal allocations, device units,
cross-task grants of memory regions, opaque IPC objects whose
SASOS-pointer would be too capability-leaky) it uses **Handles**:
opaque 32-bit identifiers backed by the per-task handle table. Handles
are a kernel-side mechanism only; programs see canonical AmigaOS
pointers. On every kernel syscall that takes a Handle, the kernel
checks:

1. Slot index is within `cap`.
2. `slots[idx].target != nullptr`.
3. `slots[idx].generation` matches the high 16 bits of the Handle (catches
   use-after-close).
4. `slots[idx].target->type` matches the type the syscall expects.

Failure of any check is a defined error (`CROI_EBADF`) returned to user
space; it is never a memory fault. This is what fixes the original AmigaOS
"ambient authority" footgun: a stale base pointer to a freed library used to
crash the system; a stale Handle here just gets `EBADF`.

A Handle is **not** portable across tasks. To share a Kobj, the owner sends
a "grant" message; the kernel allocates a slot in the recipient's table
pointing at the same Kobj and bumps its refcount. The Handle *value* is
different in the recipient.

### 5.3 Tasks (Gleas)

```c
struct Task {
    struct Kobj   hdr;                      // type = KOBJ_TASK
    char          name[32];
    uint32_t      pri;                      // -128..127, classic Exec range
    uint32_t      state;                    // ready/wait/run/dead
    uint64_t      satp;                     // root page table for this task
    uint16_t      asid;
    struct {
        uintptr_t pc, sp, gp, tp;
        uint64_t  x[32];                    // saved on trap
        uint64_t  fcsr;
        // FP state pointer; lazy-saved
    }             ctx;
    struct HandleTable handles;
    uintptr_t     stack_base;               // private slab, lower half
    size_t        stack_size;
    uint32_t      sigrecvd;                 // 32 signal bits, classic Exec
    uint32_t      sigwait;
    struct MinList  msg_ports_owned;        // intrusive, see §6
    struct ListNode sched_node;             // intrusive, in Croi run/wait queue
};
```

The `pri`, the 32-bit `sigrecvd`/`sigwait` mask, and the intrusive sched
node are deliberate echoes of the Exec `Task` structure — the ergonomics
travel.

### 5.4 Intrusive lists

Doubly-linked intrusive lists are the connective tissue of Exec. The
public API surface is verbatim AmigaOS — `struct Node`, `struct List`,
`struct MinNode`, `struct MinList`, with `AddTail` / `AddHead` /
`RemHead` / `RemTail` / `Remove` exported from `exec.library` at their
canonical LVOs (see §7). User programs `#include <exec/lists.h>` and
write `AddTail(list, node)` exactly as on the original Amiga.

For kernel-internal use we additionally provide a C23 `typeof`-based
macro form that adapts to any embedding type. These live in the brand
namespace (kernel-internal only, never exported through `exec.library`):

```c
#define MinList_AddTail(listp, nodep) do {               \
    typeof(listp) _l = (listp);                          \
    typeof(nodep) _n = (nodep);                          \
    _n->node.mln_Pred         = _l->mlh_TailPred;        \
    _n->node.mln_Succ         = (struct MinNode *)&_l->mlh_Tail; \
    _l->mlh_TailPred->mln_Succ = &_n->node;              \
    _l->mlh_TailPred          = &_n->node;               \
} while (0)
```

Sibling macros `MinList_AddHead`, `MinList_RemHead`, `MinList_RemTail`,
`MinList_Remove` cover the rest. These compile to the same memory
operations as the `AddTail`/etc. exec.library entries; they exist purely
to skip the LVO trampoline on the kernel-internal hot path.

> Note on layout: classic Exec uses the three-pointer header
> `{ lh_Head, lh_Tail, lh_TailPred }` with `lh_Tail` doubling as a
> sentinel. CaraOS preserves this exact layout — both `struct List` and
> `struct MinList` are wire-compatible with the V36+ includes — because
> programs may walk these structures field-by-field and field offsets
> are part of the ABI. (`MinList`'s `mlh_Head`/`mlh_Tail`/`mlh_TailPred`
> are the canonical AmigaOS field names.) The implementation files in
> `src/croi/` link against the same struct definitions.

---

## 6. IPC: SPSC ring buffers

### 6.1 Why this primitive

The original `PutMsg`/`GetMsg` was a linked-list push under `Disable()`. It
worked because there was no MMU and disabling interrupts was cheap. Neither
is true here. We want:

- Enqueue and dequeue with no kernel entry on the fast path.
- Wait-free for the producer and the consumer.
- Zero-copy of the message body.

The Single-Producer Single-Consumer ring buffer in shared memory hits all
three. SPSC because the classic MsgPort already has a single owning task
(the consumer); the producer-side discipline is a contract enforced by the
language, not by lock primitives.

### 6.2 Layout

A ring backs a `KOBJ_RING`. Its memory comes from the IPC pool
(§4.3) and is mapped into both producer and consumer.

```c
struct RingHeader {
    _Atomic uint32_t head;        // producer writes, consumer reads
    char             pad0[60];    // separate cache line
    _Atomic uint32_t tail;        // consumer writes, producer reads
    char             pad1[60];
    uint32_t         capacity;    // power of two; immutable after create
    uint32_t         mask;        // capacity - 1
    uint64_t         signal_kobj; // KOBJ_SIGNAL id to raise on empty→nonempty
    // slots follow:
    // struct RingSlot slots[capacity];
};

struct RingSlot {
    uint32_t   kind;     // message type, agreed between endpoints
    uint32_t   length;   // payload size in bytes
    uintptr_t  payload;  // SASOS virtual pointer; valid in both views
    uint64_t   reserved; // 32 bytes total, half a cache line
};
```

Both `head` and `tail` sit on their own cache line; the producer never
writes the consumer's line and vice versa. `capacity` is a power of two so
indexing is `slot[index & mask]`.

### 6.3 Enqueue

```c
bool Ring_Enqueue(struct RingHeader *r, struct RingSlot s) {
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if ((h - t) == r->capacity) return false;       // full
    r->slots[h & r->mask] = s;                       // payload pointer is SASOS-stable
    atomic_store_explicit(&r->head, h + 1, memory_order_release);
    if (h == t) Signal_Kobj(r->signal_kobj);          // empty → nonempty (kernel-internal raise)
    return true;
}
```

The `signal_kobj` raise is one syscall, paid only on the empty→nonempty
edge. Bursts amortize: while the consumer is draining, no further wake
syscalls occur.

### 6.4 Dequeue

```c
bool Ring_Dequeue(struct RingHeader *r, struct RingSlot *out) {
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t) return false;                        // empty
    *out = r->slots[t & r->mask];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);
    return true;
}
```

The consumer drains until empty before re-entering `Wait()` (from
`exec.library`) on its signal mask. This matches the classic MsgPort
idiom (`while ((m = GetMsg(port)))`) and gives us the same coalesced
wakeup behaviour.

### 6.5 Payload semantics

The `payload` pointer is a SASOS virtual address. The producer must:

1. Allocate the payload from a region the consumer can be given access
   to. Classic AmigaOS code uses `AllocMem(size, MEMF_PUBLIC)`; CaraOS
   honours that semantically (the resulting allocation is map-grantable
   into the recipient). For producers that already know which task will
   read the message, the CaraOS extension `AllocShared(target, size,
   flags)` (LVO past V36+, see §7.2) maps the page into the consumer's
   page table at allocation time, read-only or read-write per flags.
2. Not mutate the payload after enqueue, unless the protocol agreed with
   the consumer says it may.
3. Free the payload only after the consumer acknowledges (e.g., a reply
   message via a paired return ring).

There is no kernel-side "message ownership transfer" tracking. The
discipline is per-protocol and lives in the library that defines the ring's
`kind` codes.

### 6.6 What MsgPort becomes

A classic `MsgPort` is still a `MsgPort` — same name, same struct shape
(`mp_Node`, `mp_Flags`, `mp_SigBit`, `mp_SigTask`, `mp_MsgList`),
allocated by `CreateMsgPort()` from `exec.library`. Underneath, the
implementation is a `KOBJ_MSGPORT` Kobj that owns one inbound ring + a
signal allocation on the owning task. `PutMsg(port, msg)` resolves to
`Ring_Enqueue` + (if needed) `Signal()`; `GetMsg(port)` resolves to
`Ring_Dequeue`; `WaitPort(port)` resolves to `Wait()` on the port's
signal bit. The `_LVOPutMsg`, `_LVOGetMsg`, `_LVOWaitPort`,
`_LVOCreateMsgPort`, and `_LVODeleteMsgPort` entries in `exec.library`
are thin trampolines into these implementations.

Existing AmigaOS code patterns translate **without renames**:

```c
struct MsgPort *p = CreateMsgPort();
PutMsg(p, &msg);
WaitPort(p);
struct Message *m = GetMsg(p);
```

compiles unchanged. The CaraOS-internal symbols (`Croi_PutMsg_Impl`,
`Ring_Enqueue`, etc.) are not visible to user programs and live in the
brand namespace.

---

## 7. Libraries and Library Vector Offsets

### 7.1 The model

A CaraOS library is, on disk and in memory, the AmigaOS V36+ library
shape. Each library file (`exec.library`, `dos.library`,
`intuition.library`, `graphics.library`, `utility.library`,
`gadtools.library`, …) is mapped into the lower-half library region at
`0x0000_0000_4000_0000` (see §4.3) at a stable virtual address shared
across every task. The implementation is, on the kernel side, also a
`KOBJ_LIBRARY` so the kernel can refcount and lifecycle-manage it; user
programs do not see that.

Calling a library function follows the canonical AmigaOS pattern,
unchanged:

1. `OpenLibrary("name.library", version)` → returns a `struct Library *`
   (the library base). Because CaraOS is SASOS (§4), this pointer is
   stable across all tasks; OpenLibrary's job in CaraOS is to
   refcount-bump the library and (on first open in a task) ensure the
   library's pages are mapped into the calling task. The pointer
   itself does not need wrapping in a Handle; it is what user programs
   already pass in `A6`-equivalent (RV64: a fixed register the
   trampoline shim spills) on every call.
2. Functions live at **negative offsets** from the base — the LVO.
   On RV64 each slot holds a function pointer rather than executable
   trampoline bytes; the public stub in `<proto/<libname>.h>` lowers
   the call to a load-fnptr-from-LVO-table + `jalr`. The function
   pointer's target decides whether the function runs in-process,
   `ecall`s into Croi, or `PutMsg`s to a U-mode driver Gleas — one
   call shape, three implementation flavours. The canonical V36+ LVO
   numbers (`-30`, `-198`, `-552`, …) survive as header constants in
   `<exec/libraries.h>` and `<<library>/lvo.h>` for source-level
   V36+ compatibility and as Phase 9 binary-translator lookup keys;
   they are *not* the physical memory offsets of the function
   pointers at runtime on RV64. Full mechanism, including the
   `tools/lvo-gen` flow that generates the stubs and the per-LVO
   `local` / `syscall` / `server` flavour selection, is specified in
   `docs/LVO.md`.
3. `CloseLibrary(libraryBase)` decrements the refcount; on last close
   the kernel may unmap and tear down.

The LVO numbers are an ABI contract, not a calling-convention detail.
They match the V36+ values printed in the autodocs.

### 7.2 LVO discipline (canonical V36+ values)

Every per-library LVO matches the value in the V36+ autodocs / `.i`
include. The four reserved-per-library slots are fixed by
`<exec/libraries.h>`:

| Slot           | Offset   | Purpose                                      |
|----------------|----------|----------------------------------------------|
| `LIB_OPEN`     | `-6`     | Library Open hook                            |
| `LIB_CLOSE`    | `-12`    | Library Close hook                           |
| `LIB_EXPUNGE`  | `-18`    | Library Expunge hook                         |
| `LIB_EXTFUNC`  | `-24`    | Reserved; must return zero (per RKM)         |
| `LIB_USERDEF`  | `-30`    | First user-defined function in any library   |
| `LIB_VECTSIZE` | `6`      | Stride between successive LVOs (header constant — see note) |

> **RV64 runtime layout note.** `LIB_VECTSIZE = 6` is a 68k-era
> header constant preserved verbatim for V36+ source-level
> compatibility. The runtime function-pointer table on RV64 uses
> `sizeof(void *) = 8`-byte stride; runtime ordinals (declaration
> order in the library's `.conf`) replace negative-byte-offset
> arithmetic at dispatch time. The V36+ negative offsets remain the
> canonical wire identity for documentation and for the Phase 9
> binary translator's lookup table. See `docs/LVO.md` §3.1.

Per-library user-defined LVOs come straight from the canonical
autodocs. A representative sample for `exec.library`:

| Function         | LVO    |
|------------------|--------|
| `AllocMem`       | `-198` |
| `FreeMem`        | `-210` |
| `AddHead`        | `-240` |
| `AddTail`        | `-246` |
| `RemHead`        | `-252` |
| `RemTail`        | `-258` |
| `Remove`         | `-264` |
| `Enqueue`        | `-270` |
| `FindTask`       | `-294` |
| `SetTaskPri`     | `-300` |
| `Wait`           | `-318` |
| `Signal`         | `-324` |
| `AllocSignal`    | `-330` |
| `FreeSignal`     | `-336` |
| `PutMsg`         | `-366` |
| `GetMsg`         | `-372` |
| `ReplyMsg`       | `-378` |
| `WaitPort`       | `-384` |
| `OpenDevice`     | `-444` |
| `CloseDevice`    | `-450` |
| `DoIO`           | `-456` |
| `OldOpenLibrary` | `-408` |
| `CloseLibrary`   | `-414` |
| `OpenLibrary`    | `-552` |

This is **not** an authoritative list — `amigaos_kb_markdown/` is. The
authoritative numbers for every CaraOS library come straight from the
3rd Edition autodocs / .i files. A build-time tool (`tools/lvo-gen/`)
parses the canonical autodocs into `include/exec/libraries.h`,
`include/dos/dos.h`, `include/intuition/intuition.h`, etc., and emits
the matching trampoline table for each library; manual editing of the
LVO numbers in CaraOS source is not allowed. Drift here is an
ABI break.

CaraOS-only **extensions** to a library go at LVOs *past* the highest
V36+ slot the autodocs document. They are clearly marked as
extensions and never collide with classic numbers. Examples — none of
which exist in V36+ exec, all of which CaraOS may add:

- `AllocShared(target, size, flags)` — memory visible in the target
  task as well as the caller. Useful for the IPC payload pattern
  (§6.5). LVO: first free slot past the V36+ exec range.

If a CaraOS extension would *replace* an AmigaOS function, don't —
extend instead, and let the V36+ original keep its slot. Programs that
don't know about the extension still work.

### 7.3 ABI-frozen structures

These structs are part of the public ABI and must not change layout
once v1.0 ships. Their definitions are verbatim AmigaOS V36+:

- **`struct Node`, `struct MinNode`** (from `<exec/nodes.h>`) — fields
  `ln_Succ`/`ln_Pred`/`ln_Type`/`ln_Pri`/`ln_Name`, and
  `mln_Succ`/`mln_Pred`.
- **`struct List`, `struct MinList`** (from `<exec/lists.h>`) — three-pointer
  header `lh_Head`/`lh_Tail`/`lh_TailPred` and the minimal
  `mlh_Head`/`mlh_Tail`/`mlh_TailPred`.
- **`struct Library`** (from `<exec/libraries.h>`) — the library base
  prefix every library exposes (`lib_Node`, `lib_Flags`, `lib_pad`,
  `lib_NegSize`, `lib_PosSize`, `lib_Version`, `lib_Revision`,
  `lib_IdString`, `lib_Sum`, `lib_OpenCnt`).
- **`struct Task`** (from `<exec/tasks.h>`) — `tc_Node`, `tc_Flags`,
  `tc_State`, `tc_IDNestCnt`, `tc_TDNestCnt`, `tc_SigAlloc`,
  `tc_SigWait`, `tc_SigRecvd`, `tc_SigExcept`, `tc_TrapAlloc`,
  `tc_TrapAble`, `tc_ExceptData`, `tc_ExceptCode`, `tc_TrapData`,
  `tc_TrapCode`, `tc_SPReg`, `tc_SPLower`, `tc_SPUpper`, `tc_Switch`,
  `tc_Launch`, `tc_MemEntry`, `tc_UserData`. Anything kernel-private
  (page tables, RV64 saved register tile, ASID) hangs off `tc_UserData`
  or behind an internal sibling structure — never inside `struct Task`.
- **`struct MsgPort`** (from `<exec/ports.h>`) — `mp_Node`, `mp_Flags`,
  `mp_SigBit`, `mp_SigTask`, `mp_MsgList`. The CaraOS implementation
  attaches a ring/signal Kobj to the port out-of-band; `struct MsgPort`
  itself stays the canonical V36+ shape.
- **`struct Message`** (from `<exec/ports.h>`) — `mn_Node`,
  `mn_ReplyPort`, `mn_Length`. Every IPC payload begins with this
  prefix.
- **`struct IORequest`, `struct IOStdReq`** (from `<exec/io.h>`).
- **`struct RastPort`, `struct BitMap`** (from `<graphics/rastport.h>`,
  `<graphics/gfx.h>`).
- **`struct Window`, `struct Screen`, `struct IntuiMessage`**, **`struct
  Gadget`**, **`struct Menu`** (from `<intuition/intuition.h>` and
  friends).
- **`struct TagItem`** (from `<utility/tagitem.h>`).

Internal-only structures used to back these (`struct RingHeader`,
`struct RingSlot`, `struct Kobj`, `struct HandleTable`, …) belong to
the brand namespace and may evolve. They never appear in user-program
headers.

---

## 8. Boot handoff (Splanc → Croi)

### 8.1 Steps

1. **U-Boot** finds and launches `\EFI\BOOT\BOOTRISCV64.EFI`, which is
   `splanc.efi` renamed for the UEFI fallback path. We supply UEFI Boot
   Services; we are a UEFI Application.
2. **Splanc** does, in this order:
    1. Locates the **Device Tree** by walking the EFI Configuration Table
       for the `EFI_DTB_TABLE_GUID` entry. The DTB pointer is saved.
    2. Locates the **Croi kernel image**. v0 embeds it as a `.croi_image`
       PE section in `splanc.efi`; v1 will load it from a known path on
       the ESP. Embedding eliminates one filesystem dependency for the
       first bring-up.
    3. Calls `GetMemoryMap` and constructs the initial **page tables**:
       a kernel upper-half mapping, an identity map of the trampoline
       page, and a temporary lower-half mapping for the Croi image's
       load region while it is being copied to its final upper-half home.
    4. Calls `ExitBootServices` with the saved memory map key.
    5. From this point Splanc is running with UEFI gone. It writes
       `satp` to enable Sv39 (mode 8), executes `sfence.vma`, and jumps
       to the trampoline page.
    6. The trampoline strips the lower-half identity mapping and tail-calls
       the Croi entry symbol.
3. **Croi entry** receives, by RISC-V convention:
    - `a0` = the booting **hartid**
    - `a1` = the **physical address of the DTB**
4. **Croi** initializes:
    1. Set up the trap vector (`stvec`) and per-hart trap stack.
    2. Initialize the physical frame allocator from the memory map ranges
       Splanc encoded into a small handoff blob.
    3. Initialize the kernel heap (slab + buddy hybrid; details TBD).
    4. Hand the DTB pointer to the FDT parser (§9) to populate
       `struct CroiPlatform`. The console UART, CLINT, PLIC, memory
       banks, reserved regions, and per-hart ISA features are all
       discovered there — no addresses are hard-coded in Croi.
    5. Bring up secondary harts via SBI HSM `hart_start`. Each new hart
       lands at the same Croi entry with its own `a0`, takes the
       per-hart-stack slot keyed by hartid, and joins the scheduler.
    6. Allocate the bootstrap Task, attach a handle table, and start
       `guth` (the CLI) as the first Gleas. Stdin/stdout are wired to
       the console UART through `exec.library`'s console port.

### 8.2 Splanc handoff blob

Between calling `ExitBootServices` and entering Croi, Splanc constructs a
compact handoff blob (placed at a fixed kernel-virtual address):

```c
struct SplancHandoff {
    uint32_t magic;            // 'SPLC' = 0x53504C43
    uint32_t version;
    uint64_t dtb_phys;
    uint64_t kernel_phys_base; // where the Croi image was loaded
    uint64_t kernel_phys_end;
    uint32_t mmap_count;
    uint32_t mmap_stride;
    // followed by mmap_count entries of {uint64_t base, uint64_t size, uint32_t kind, uint32_t attr;}
};
```

This is what Croi consumes during early init. The DTB itself is the
authoritative description of the rest of the platform.

---

## 9. Device tree handling

Both Splanc and Croi need to know the platform's memory map, console UART
location, CLINT and PLIC base addresses, IRQ numbering, and per-hart
capabilities. None of that is hard-coded. All of it is read from the
**Flattened Device Tree (FDT)** that U-Boot hands to Splanc and Splanc
forwards to Croi (`a1` register at S-mode entry, also recorded in the
handoff blob — see §8.2).

### 9.1 Why a parser

Hard-coding the X1's MMIO map would lock CaraOS to a single board revision
and silently break when the vendor moves a peripheral. A parser also makes
QEMU `-machine virt` boot work without a separate code path: QEMU's UART
lives at `0x1000_0000` and is `ns16550a`-compatible; the X1's lives at
`0xD401_7000` and is `ky,pxa-uart`. The parser handles both with the same
Croi binary.

### 9.2 Scope (v0)

The minimum subset needed to bring up Croi:

- enumerate `/memory@*` ranges
- enumerate `/reserved-memory/*` (must-not-allocate)
- enumerate `/cpus/cpu@*` — hartid, ISA string, `mmu-type`, cache geometry
- find the node compatible with `"riscv,clint0"` → CLINT base / size
- find the node compatible with `"riscv,plic0"` → PLIC base / size, `riscv,ndev`
- resolve `/chosen/stdout-path` to a UART node → console base, IRQ,
  `reg-shift`, `reg-io-width`, `clock-frequency` (or computed via the
  clock graph; see `docs/DTS_PARSER.md`), `current-speed` / baud
- read `/chosen/bootargs` (not consumed by Croi v0, but stashed for Guth)

Anything beyond this — full clock graph traversal, regulators, GPIO
controllers, pin mux — is deferred to driver modules, each of which
re-opens the FDT to walk the subtree it cares about.

### 9.3 Implementation strategy

The parser is **written from scratch** against the *Devicetree
Specification* (release 0.4 or later). We do not vendor `libfdt`, even
though it's permissively licensed: the parts of FDT we need are small, and
a from-scratch walker keeps CaraOS source under a single license and a
single style. `libfdt` remains a useful reference implementation to
cross-check against during testing.

Lives at `src/croi/fdt/` (header in `include/cara/fdt.h`). It is:

- **Read-only**: never modifies the blob.
- **Allocation-free**: callers pass any output buffers; the parser never
  calls into the kernel heap. This makes it safe to use before the heap
  exists.
- **MMU-free**: the API takes physical or virtual pointers indifferently
  — Splanc calls it pre-paging, Croi calls it post-paging.
- **Big-endian aware**: FDT integers are big-endian on disk regardless of
  host endianness; the parser handles the byte-swap.
- **Host-buildable**: the same `.c` files compile under the hosted CMake
  build, exercised by unit tests against captured DTBs in `tests/data/`.

API sketch:

```c
struct Fdt;                        /* opaque view over a blob */

[[nodiscard]] int Fdt_Open(struct Fdt *out, const void *blob);

[[nodiscard]] int Fdt_FindByCompatible(const struct Fdt *,
                                       const char *compat,
                                       uint32_t   *node_off_inout);

[[nodiscard]] int Fdt_ResolvePath(const struct Fdt *, const char *path,
                                  uint32_t *node_off_out);

[[nodiscard]] int Fdt_PropU64(const struct Fdt *, uint32_t node,
                              const char *name, uint64_t *out);

[[nodiscard]] int Fdt_PropReg(const struct Fdt *, uint32_t node,
                              uint32_t index, uint64_t *base_out,
                              uint64_t *size_out);

[[nodiscard]] const char *Fdt_PropStr(const struct Fdt *, uint32_t node,
                                      const char *name);

[[nodiscard]] int Fdt_ChildIter(const struct Fdt *, uint32_t parent,
                                uint32_t *cursor_inout);
```

Detailed format coverage, traversal semantics, and the test plan live in
`docs/DTS_PARSER.md`.

### 9.4 Where the data ends up

After Croi has parsed the FDT during early init, results are stored in a
flat C struct that subsequent modules read from instead of re-traversing
the FDT:

```c
struct CroiPlatform {
    uint64_t timebase_hz;

    uint32_t hart_count;
    struct {
        uint32_t hartid;
        bool     has_rvv, has_sstc, has_svnapot, has_svinval;
        bool     has_zicbom, has_zicboz, has_zba, has_zbb;
        uint32_t cbom_block_size;        /* bytes */
        uint32_t l1d_size, l1d_line, l1d_sets;
        uint32_t l1i_size, l1i_line, l1i_sets;
    } harts[16];

    struct { uint64_t base, size; } mem_banks[8];
    uint32_t mem_bank_count;

    struct { uint64_t base, size; } reserved[16];
    uint32_t reserved_count;

    struct { uint64_t base, size; } clint;
    struct { uint64_t base, size; uint32_t ndev, max_priority; } plic;

    struct {
        uint64_t base;
        uint32_t irq;
        uint32_t reg_shift;
        uint32_t reg_io_width;
        uint32_t clock_hz;
        uint32_t baud;
        char     compatible[32];
    } console;

    const char *bootargs;
};
```

This is the single point where "what does the hardware look like?" gets
answered. Past early init, nothing reaches into the FDT; everything reads
from this struct or a module-specific peer of it.

---

## 10. C23 conventions

- **Standard**: `-std=c23` strictly. No GNU extensions used outside of
  `__attribute__((aligned))` (which has a C23 standardized form
  `[[gnu::aligned(N)]]` we will use), `__attribute__((naked))` for the
  trap entry stub, and inline assembly.
- **`nullptr`**: used everywhere a null pointer is meant. `NULL` is not
  defined in CaraOS headers.
- **`typeof`**: for intrusive list macros and any container_of-style
  primitive.
- **`constexpr`**: for LVO numbers, struct sizes used in linker scripts,
  and any compile-time table sizing.
- **`[[nodiscard]]`**: on every function that returns a Handle, an error
  code, or an allocation pointer. The compiler tells you when you forget
  to check.
- **`[[gnu::aligned(64)]]`**: on every struct that includes lock-free
  atomic state intended to live on its own cache line.
- **Designated initializers**: required in any struct literal with more
  than two fields.
- **Atomics**: from `<stdatomic.h>` only. No hand-rolled memory fences.
- **No legacy headers**: no `string.h`/`stdlib.h` in the kernel; we ship
  `cara/string.h` and `cara/mem.h` with the subset we want.
- **Toolchain**: clang ≥ 18 is the reference compiler (full C23). GCC ≥ 14
  is supported as a secondary; any feature gap is documented in
  `docs/TOOLCHAIN.md`.

---

## 11. Module layout

```
cara-os/
├── amigaos_kb_markdown/        (existing) functional spec — read, do not copy
├── docs/
│   ├── ARCHITECTURE.md         (this file)
│   ├── BOOT.md                 (TBD: detailed Splanc / Croi handoff)
│   ├── IPC.md                  (TBD: ring buffer protocol details)
│   ├── LVO.md                  library / driver bridge: the lvo-gen model
│   └── TOOLCHAIN.md            (TBD: clang/GCC matrix)
├── include/                    Two-namespace split (PRINCIPLES.md §3.1):
│   ├── cara/                   BRAND namespace — kernel-internal only.
│   │   ├── fdt.h               FDT parser API (read-only, alloc-free)
│   │   ├── handle.h            Handle type, error codes
│   │   ├── platform.h          struct CroiPlatform (post-FDT-parse snapshot)
│   │   ├── ring.h              RingHeader + Ring_Enqueue/Dequeue (internal)
│   │   ├── kobj.h              Kobj base, type tags
│   │   └── types.h             Cara fixed-width types, attributes
│   ├── exec/                   API namespace — verbatim AmigaOS V36+
│   │   ├── lists.h             struct Node/List/MinNode/MinList, AddTail, …
│   │   ├── memory.h            AllocMem, FreeMem, MEMF_*
│   │   ├── ports.h             struct MsgPort/Message, PutMsg, GetMsg, …
│   │   ├── tasks.h             struct Task, Wait, Signal, AllocSignal, …
│   │   ├── libraries.h         struct Library, OpenLibrary, CloseLibrary, …
│   │   ├── io.h                struct IORequest, OpenDevice, DoIO, …
│   │   ├── nodes.h             NT_* node types
│   │   ├── semaphores.h
│   │   └── types.h             ULONG, UWORD, BPTR, APTR — AmigaOS-shape
│   ├── dos/                    API namespace — `dos.library` surface
│   ├── intuition/              API namespace — `intuition.library` surface
│   ├── graphics/               API namespace — `graphics.library` surface
│   │   ├── rastport.h          struct RastPort, Move, Draw, RectFill, …
│   │   ├── gfx.h               struct BitMap, BLITTER ops
│   │   └── …
│   ├── utility/                API namespace — `utility.library` (V36+) tags
│   ├── libraries/              API namespace — gadtools/asl/iffparse/etc.
│   ├── devices/                API namespace — `*.device` surfaces
│   └── workbench/              API namespace — icon.library, .info shapes
├── src/
│   ├── splanc/                 UEFI bootstrap (splanc.efi)
│   ├── croi/                   Kernel: scheduler, MMU, handles, IPC, syscalls,
│   │   │                       implements `exec.library` underneath
│   │   ├── fdt/                FDT parser + struct CroiPlatform population
│   │   └── lib/                exec.library trampolines, LVO table
│   ├── logaic/                 Implements `dos.library`
│   ├── leargas/                Implements `intuition.library`
│   ├── dath/                   Implements `graphics.library` + GPU driver glue
│   ├── clar/                   Workbench analogue (the desktop environment)
│   ├── guth/                   CLI shell (uses dos.library + console.device)
│   └── libcara/                Userspace runtime stubs that resolve LVO calls
├── boards/
│   └── orangepi-rv2/           Linker offsets, bring-up notes, captured DTBs
├── tools/
│   ├── lvo-gen/                Parses RKM autodocs → public headers
│   │                           and library trampoline tables
│   └── img-build/              Builds an ESP image with splanc.efi + croi.bin
└── tests/
    ├── unit/                   Hosted-build unit tests of pure data structures
    └── boot/                   QEMU boot smoke tests
```

The directory tree under `include/` is the brand-vs-API split made
concrete: `include/cara/*` is what kernel code includes; `include/exec/*`,
`include/dos/*`, `include/intuition/*`, `include/graphics/*` etc. are
what user programs include — and a 1992 program's
`#include <exec/lists.h>` finds the same surface.

`src/croi` is the only module that runs in S-mode; everything in
`src/leargas`, `src/dath`, `src/logaic`, `src/clar`, `src/guth`, and the
other library implementations link against `libcara` and run as
Gleasanna in U-mode. The brand-namespace source-directory names
(`logaic`/`leargas`/`dath`/etc.) describe *which CaraOS team owns the
implementation*; the produced binary is the API-namespace library
(`dos.library`/`intuition.library`/`graphics.library`).

---

## 12. Build, test, run (sketch)

- **Host build** of pure data structures and ring buffer logic compiles
  on macOS/Linux x86_64 with the same `-std=c23` and runs unit tests.
  This catches list and ring bugs before any hardware is involved.
- **Cross build** uses `clang --target=riscv64-unknown-elf` with the
  in-tree linker scripts under `boards/<board>/`. Output: `splanc.efi`,
  `croi.elf`, and an ESP image.
- **QEMU** boots the ESP image under `qemu-system-riscv64 -bios default`
  (which provides OpenSBI) and `-machine virt`. This is the per-commit
  smoke test.
- **Hardware** boot is via SD card on VisionFive 2 / OrangePi RV2 with
  the ESP image written to the appropriate partition. Console is the
  board's debug UART.

Detailed build instructions live in `docs/TOOLCHAIN.md` (TBD) — this
section is intentionally a sketch.

---

## 13. Glossary

### 13.1 Brand namespace (CaraOS-internal — see PRINCIPLES.md §3.1)

These names appear in the project, source directories, the kernel
binary, internal symbols, and prose. They never appear in the public
API surface a user program references.

| CaraOS    | Irish meaning      | What it implements                     | API library / binary it produces |
|-----------|--------------------|----------------------------------------|----------------------------------|
| Cara      | "friend"           | The OS itself                          | (whole product)                  |
| Croi      | "heart"            | Microkernel, scheduler, MMU, IPC, handles | `croi.elf` + `exec.library`   |
| Splanc    | "flash, spark"     | UEFI bootstrap                         | `splanc.efi`                     |
| Logaic    | "logic"            | Volumes, locks, files, drawers         | `dos.library`                    |
| Leargas   | "insight, clarity" | Windowing, IDCMP, gadgets, screens     | `intuition.library`              |
| Dath      | "colour"           | RastPort drawing, RTG-style GPU driver | `graphics.library`               |
| Clar      | "board, surface"   | Desktop / file environment             | (Workbench analogue Gleas)       |
| Guth      | "voice"            | CLI shell                              | (Guth Gleas, like the original CLI) |
| Bosca     | "box"              | Container concept (a "drawer")         | (UI vocabulary, not a library)   |
| Inntin    | "mind, intent"     | Interactive UI element concept         | (UI vocabulary; struct Gadget)   |
| Gleas     | "tool, apparatus"  | An executable                          | (file kind, not a library)       |

A CaraOS hacker reading kernel source sees brand-namespace names
everywhere. A user-program author including `<exec/libraries.h>` and
calling `OpenLibrary("intuition.library", 36)` never has to know any of
the brand names exist.

### 13.2 API namespace (verbatim AmigaOS V36+)

The libraries, devices, struct names, function names, and LVO offsets
are spelled exactly as the 3rd Edition autodocs print them. No
exhaustive enumeration here — the autodocs in `amigaos_kb_markdown/`
are the spec. The library and device filenames CaraOS ships are
listed in `PRINCIPLES.md §3.1`.

### 13.3 General terms

| Term      | Meaning |
|-----------|---------|
| **SASOS** | Single Address Space Operating System; one virtual address space, per-task page tables differ only in which mappings they expose. |
| **LVO**   | Library Vector Offset; negative byte offset from a library base to its function trampoline. |
| **Kobj**  | Kernel object; the typed thing handles point at. |
| **Handle**| 32-bit (generation, slot) identifier for a Kobj, valid only in the issuing task. |
| **Hart**  | RISC-V Hardware Thread; what the rest of the world calls a CPU core / SMT thread. |
| **SBI**   | Supervisor Binary Interface; the M-mode services we call by `ecall`. |
| **DTB**   | Device Tree Blob; the platform description handed to us by U-Boot. |

---

## 14. Open questions

These are explicitly unresolved and will be revisited as implementation
exposes them.

1. **Sv48 migration.** Whether to ever go past Sv39 on X1 (the X1's MMU
   advertises only `riscv,sv39` per the DT, so this is a forward-port
   question for a future SoC, not a v0 concern).
2. **ASID width** on the X60. Measured at boot rather than assumed; this
   question is just about whether 16-bit, 9-bit, or smaller, and whether
   the recycle policy needs to bias toward longer-lived tasks.
3. **Stack overflow detection.** Guard pages on per-task stacks are easy;
   reporting them as `Guru Meditation` rather than a silent kill needs a
   dedicated trap path.
4. **Hot-path syscall encoding.** **Resolved (2026-05-08, see
   `docs/LVO.md`).** Every library function is a function-pointer
   call through its LVO table; the pointer's target picks the
   dispatch — `local` (in-process call), `syscall` (a tiny `ecall`
   stub), or `server` (`PutMsg` round-trip to a Gleas). `PutMsg`
   itself is a `syscall`-flavoured `exec.library` LVO that
   short-circuits into the producer-side ring update and only traps
   if the consumer needs a wake — the ring primitive in §6 already
   handles that edge.
5. **Filesystem.** Logaic v0 will likely target FAT (since the ESP is
   already FAT and U-Boot understands it). A native CaraFS is a
   post-v0 question.
6. **SMP scheduling.** v0 is single-hart at boot; secondary harts come
   up parked. The scheduler model (per-hart runqueue + work stealing
   vs. global queue) is deferred to when we actually have a load to
   measure. The X1's two-cluster, 4+4 topology may push us toward
   per-cluster runqueues.
7. **Driver model.** **Mechanism resolved per-LVO (2026-05-08, see
   `docs/LVO.md`); flavour-per-LVO still a per-library design call.**
   Devices in classic Exec were units behind `OpenDevice`; that maps
   cleanly to Handles + Kobjs here. Each library's `.conf` declares
   per-LVO whether the implementation is `local` (in-process),
   `syscall` (in Croi), or `server` (a U-mode driver Gleas), and a
   library may mix flavours freely. *Which* flavour for *which* LVO
   is decided when each library's `.conf` is authored — stability
   argues `server` for hardware-touching operations; latency may
   keep specific hot paths in `syscall`.
8. **RVV in the kernel.** Using V for `memcpy`/`memset` and page-clear
   inside Croi is attractive (X1 has it), but it grows the per-trap save
   surface unless we gate kernel V use behind explicit windows that
   clear `sstatus.VS` to Off before returning to U-mode. To be measured.

---
