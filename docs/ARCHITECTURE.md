# CaraOS Architecture

A cleanroom C23/RISC-V reimplementation of the AmigaOS Release 2
specification (the 3rd Edition RKMs in `amigaos_kb_markdown/`, V36+).

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

---

## 1. Goals and non-goals

### 1.1 Goals

1. **Functional fidelity to AmigaOS Release 2 (V36+).** The contracts of
   Exec, AmigaDOS, and Intuition — message ports, libraries, LVO jumps,
   intrusive lists, signals, tasks, drawers, gadgets, screens, windows —
   plus the Release 2 additions (utility, gadtools, asl, BOOPSI, iffparse,
   commodities, icon, diskfont) are reproduced under the CaraOS names
   (Croi, Logaic, Leargas, Clar, Guth, Bosca, Inntin, Gleas). Behavioural
   parity, not source parity. The 3rd Edition RKMs in
   `amigaos_kb_markdown/` are the spec.
2. **Pointer-passing IPC ergonomics.** A producer hands a virtual pointer to a
   consumer and the consumer dereferences it. No marshalling, no serialization,
   no copy. This is what made the original system feel fast and what we keep.
3. **MMU-assisted stability.** A user `Gleas` (Tool) that faults must not take
   down the `Croi` kernel or other Gleasanna. Isolation is used for fault
   containment, not for security or multi-tenant separation.
4. **Cleanroom from the RKMs.** The markdown copies in `amigaos_kb_markdown/`
   are the spec we read; we never copy text or code from the original sources
   into CaraOS.
5. **Stock RISC-V SoCs only.** Targets: StarFive **VisionFive 2** (JH7110) and
   **OrangePi RV2** (Spacemit K1). No custom hardware, no custom chipset
   emulation. We stand on U-Boot, OpenSBI, and UEFI.
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
"this Gleas" is "allowed to" open `logaic.library`. If you have a handle, you
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
| `0x0000_0000_4000_0000`    | 4 GiB     | **Shared library text/rodata.** `croi.library`, `logaic.library`, `leargas.library`, etc. live here at fixed virtual addresses. | RX, all tasks |
| `0x0000_0001_0000_0000`    | 64 GiB    | **Shared system heap.** `Croi_Alloc` allocates from here. Every allocation lives at a stable virtual address. | RW *only* on the owning task; no-access elsewhere. Tasks that need a peer's allocation receive an explicit map-grant via Croi syscall. |
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
   it is freed. This is what makes `Croi_Send` zero-copy.

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

`Croi_OpenLib`, `Croi_Alloc` (when an allocation is exposed as a memregion
Kobj for sharing), `Croi_CreatePort`, etc. return Handles. On every Croi
syscall that takes a Handle, the kernel checks:

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

Doubly-linked intrusive lists are the connective tissue of Exec. We keep
them, but C23 lets us drop the manual casts:

```c
struct ListNode {
    struct ListNode *succ;
    struct ListNode *pred;
};

struct MinList {
    struct ListNode  head;     // head.succ → first real node
    struct ListNode  tail;     // tail.pred → last real node
    // head.pred and tail.succ are nullptr; sentinels guard both ends
};

#define Croi_AddTail(listp, nodep) do {                  \
    typeof(listp) _l = (listp);                          \
    typeof(nodep) _n = (nodep);                          \
    _n->node.pred       = _l->tail.pred;                 \
    _n->node.succ       = &_l->tail;                     \
    _l->tail.pred->succ = &_n->node;                     \
    _l->tail.pred       = &_n->node;                     \
} while (0)
```

The `typeof` form makes the macro symmetric across whichever struct embeds
a `ListNode` named `node`. We will provide `Croi_AddHead`, `Croi_RemHead`,
`Croi_RemTail`, and `Croi_Remove` with the same shape.

> Note: this implementation uses a head + tail sentinel layout. Classic
> Exec used a single header with `lh_Head`, `lh_Tail`, `lh_TailPred` where
> the `lh_Tail` field doubled as a sentinel. The two are isomorphic; the
> sentinel form is easier to read and to verify. The wire-level layout
> matters only for `croi.library` exported types — see §7 for which structs
> are ABI-frozen.

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
    if (h == t) Croi_Signal(r->signal_kobj);          // empty → nonempty
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

The consumer drains until empty before re-entering `Croi_Wait` on its
signal mask. This matches the classic MsgPort idiom (`while ((m = GetMsg))`)
and gives us the same coalesced wakeup behavior.

### 6.5 Payload semantics

The `payload` pointer is a SASOS virtual address. The producer must:

1. Allocate the payload from a region the consumer can be given access to
   (typically `Croi_AllocShared(target_task, size)`, which maps the page
   into the consumer's page table read-only or read-write per the producer's
   choice).
2. Not mutate the payload after enqueue, unless the protocol agreed with
   the consumer says it may.
3. Free the payload only after the consumer acknowledges (e.g., a reply
   message via a paired return ring).

There is no kernel-side "message ownership transfer" tracking. The
discipline is per-protocol and lives in the library that defines the ring's
`kind` codes.

### 6.6 What MsgPort becomes

A classic `MsgPort` is now: a `KOBJ_MSGPORT` Kobj that owns one inbound
ring + a signal allocation on the owning task. `Croi_Send(port, msg)`
resolves to `Ring_Enqueue` + (if needed) `Croi_Signal`. `Croi_Receive(port)`
resolves to `Ring_Dequeue`. The `LVO_Send` and `LVO_Recv` entries in
`croi.library` are thin wrappers around these. Existing code patterns
(`PutMsg(port, &msg); WaitPort(port); m = GetMsg(port);`) translate
line-for-line into the Croi names.

---

## 7. Libraries and Library Vector Offsets

### 7.1 The model

A CaraOS library is a Kobj of type `KOBJ_LIBRARY` plus a fixed-address
mapping of its `.text`/`.rodata` in the lower half (region at
`0x0000_0000_4000_0000` — see §4.3). Calling a library function is:

1. User has a `Handle` to the library, returned by `Croi_OpenLib("croi", 0)`.
2. The library's *base* (a virtual address in the shared library region) is
   stored in the slot, available via `Croi_HandleBase(h)`.
3. Functions live at **negative offsets** from the base — the LVO. The
   first function is at `base - 6` in classic Exec; we keep this. On
   RISC-V, "−6" addresses a 6-byte slot containing a single trampoline
   instruction sequence (a 4-byte `auipc` + 2-byte `c.jr`, or a 4-byte
   `jal` to a fixed target — final encoding chosen by the linker script).

The LVO numbers are not a calling convention detail; they are an ABI
contract. We encode them as `constexpr int32_t` so that any code
referencing them does the math at compile time and the resulting jump
table is a static slice of read-only data.

### 7.2 Croi LVO map (initial)

The numbers below are aligned with classic Exec where the function
exists in both. New Croi-only functions get fresh slots beyond the
classic range.

| Croi function       | LVO     | Original Exec analogue | Notes                            |
|---------------------|---------|------------------------|----------------------------------|
| `Croi_Open`         | `-30`   | `Open` (lib base op)   | Reserved per RKM convention      |
| `Croi_Close`        | `-36`   | `Close`                |                                  |
| `Croi_Expunge`      | `-42`   | `Expunge`              |                                  |
| `Croi_Reserved`     | `-48`   | `Reserved`             | Always 0; per spec               |
| `Croi_AddHead`      | `-240`  | `AddHead`              |                                  |
| `Croi_AddTail`      | `-246`  | `AddTail`              |                                  |
| `Croi_RemHead`      | `-258`  | `RemHead`              |                                  |
| `Croi_RemTail`      | `-264`  | `RemTail`              |                                  |
| `Croi_Alloc`        | `-198`  | `AllocMem`             | Returns SASOS virtual pointer    |
| `Croi_Free`         | `-210`  | `FreeMem`              |                                  |
| `Croi_AllocShared`  | `-696`  | (new)                  | Maps into a target task too      |
| `Croi_CreatePort`   | `-354`  | `CreatePort` (amiga.lib) Croi-side built-in |       |
| `Croi_DeletePort`   | `-360`  |                        |                                  |
| `Croi_Send`         | `-366`  | `PutMsg`               |                                  |
| `Croi_Receive`      | `-372`  | `GetMsg`               |                                  |
| `Croi_WaitPort`     | `-384`  | `WaitPort`             |                                  |
| `Croi_Signal`       | `-324`  | `Signal`               |                                  |
| `Croi_Wait`         | `-318`  | `Wait`                 |                                  |
| `Croi_OpenLib`      | `-552`  | `OpenLibrary`          | Returns a Handle, not a base     |
| `Croi_CloseLib`     | `-414`  | `CloseLibrary`         |                                  |
| `Croi_HandleBase`   | `-702`  | (new)                  | Resolves Handle → library base   |

In C23:

```c
constexpr int32_t LVO_Open       = -30;
constexpr int32_t LVO_Close      = -36;
constexpr int32_t LVO_Alloc      = -198;
constexpr int32_t LVO_Free       = -210;
constexpr int32_t LVO_Send       = -366;
constexpr int32_t LVO_Receive    = -372;
constexpr int32_t LVO_OpenLib    = -552;
constexpr int32_t LVO_HandleBase = -702;
// ...
```

A `lvo.h` header in `include/cara/` will own the canonical list. Generating
the assembly trampoline table from this header is a build-time step; no
runtime reflection required.

### 7.3 ABI-frozen structures

These structs are part of the public ABI and must not change layout once
v1.0 ships:

- `struct ListNode`, `struct MinList`
- `struct RingHeader`, `struct RingSlot`
- `struct Message` (the prefix every IPC payload starts with)
- `struct Task` *visible portion* — the kernel-private portion sits behind
  an opaque pointer; the visible portion holds `name`, `pri`, `sigrecvd`,
  and the embedded `MinList` of owned ports.

Everything else is internal and may move.

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
       the console UART through `croi.library`'s console port.

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
  calls `Croi_Alloc`. This makes it safe to use before the heap exists.
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
│   ├── LVO.md                  (TBD: full LVO table + generation script)
│   └── TOOLCHAIN.md            (TBD: clang/GCC matrix)
├── include/
│   └── cara/
│       ├── fdt.h               FDT parser API (read-only, alloc-free)
│       ├── handle.h            Handle type, error codes
│       ├── list.h              ListNode/MinList + typeof macros
│       ├── lvo.h               constexpr LVO numbers
│       ├── msgport.h           Message, MsgPort, ring slot kinds
│       ├── platform.h          struct CroiPlatform (post-FDT-parse snapshot)
│       ├── ring.h              RingHeader + Ring_Enqueue/Dequeue
│       └── types.h             Cara fixed-width types, attributes
├── src/
│   ├── splanc/                 UEFI bootstrap (splanc.efi)
│   ├── croi/                   Kernel: scheduler, MMU, handles, IPC, syscalls
│   │   └── fdt/                FDT parser + struct CroiPlatform population
│   ├── logaic/                 DOS layer: volumes, locks, files, drawers
│   ├── leargas/                Windowing: screens, windows, gadgets, events
│   ├── clar/                   Desktop / file environment (Workbench analogue)
│   ├── guth/                   CLI shell
│   └── libcara/                Userspace runtime stubs that resolve LVO calls
├── boards/
│   └── orangepi-rv2/           Linker offsets, bring-up notes, captured DTBs
├── tools/
│   ├── lvo-gen/                Generates lvo.h + library trampoline tables
│   └── img-build/              Builds an ESP image with splanc.efi + croi.bin
└── tests/
    ├── unit/                   Hosted-build unit tests of pure data structures
    └── boot/                   QEMU boot smoke tests
```

`src/croi` is the only module that runs in S-mode; everything in `src/leargas`,
`src/clar`, `src/guth`, and the `*.library` modules link against `libcara`
and run as Gleasanna in U-mode.

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

| CaraOS    | Irish meaning      | Amiga analogue         |
|-----------|--------------------|------------------------|
| Cara      | "friend"           | AmigaOS                |
| Croi      | "heart"            | Exec / kernel          |
| Logaic    | "logic"            | AmigaDOS               |
| Leargas   | "insight, clarity" | Intuition              |
| Clar      | "board, surface"   | Workbench              |
| Splanc    | "flash, spark"     | Kickstart              |
| Guth      | "voice"            | CLI / Shell            |
| Bosca     | "box"              | Drawer                 |
| Inntin    | "mind, intent"     | Gadget                 |
| Gleas     | "tool, apparatus"  | Tool / executable      |

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
4. **Hot-path syscall encoding.** `ecall` + a single register selector
   vs. an LVO-style virtual-address jump that traps. The classic pattern
   was just a JSR through the library base; the modern pattern is a
   trapped syscall. We may end up with both: `Croi_Send` short-circuits
   into a userspace ring update and only traps if the consumer needs a
   wake.
5. **Filesystem.** Logaic v0 will likely target FAT (since the ESP is
   already FAT and U-Boot understands it). A native CaraFS is a
   post-v0 question.
6. **SMP scheduling.** v0 is single-hart at boot; secondary harts come
   up parked. The scheduler model (per-hart runqueue + work stealing
   vs. global queue) is deferred to when we actually have a load to
   measure. The X1's two-cluster, 4+4 topology may push us toward
   per-cluster runqueues.
7. **Driver model.** Devices in classic Exec were units behind
   `OpenDevice`; that maps cleanly to Handles + Kobjs here. The open
   question is whether drivers run as Gleasanna (U-mode + IOMMU/PMP) or
   as Croi-linked S-mode modules. Stability argues U-mode; latency may
   force some hot drivers into S-mode.
8. **RVV in the kernel.** Using V for `memcpy`/`memset` and page-clear
   inside Croi is attractive (X1 has it), but it grows the per-trap save
   surface unless we gate kernel V use behind explicit windows that
   clear `sstatus.VS` to Off before returning to U-mode. To be measured.

---
