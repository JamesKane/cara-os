# L7 — BOOPSI (intuition class system)

> Scope/design for Phase 3 epic **L7**: the **B**asic **O**bject-**O**riented
> **P**rogramming **S**ystem for **I**ntuition — the runtime class/object
> machinery in `intuition.library` that gadtools (L8) and asl (L9) are
> built on. Read this before cutting L7 code. Pairs with
> `docs/LEARGAS_INTUITION.md` (the L5 window/menu/requester surface this
> extends), `docs/LVO.md` (the dispatch/flavour model), and
> `docs/PHASE3.md` (the library-by-library plan).
>
> The authoritative spec is the AmigaOS V36+ `<intuition/classes.h>`,
> `<intuition/classusr.h>`, `<intuition/gadgetclass.h>`,
> `<intuition/imageclass.h>` and the RKM "Boopsi" chapter (read from
> `amiga_docs/`, never copy). Verbatim field names, offsets, method IDs,
> and tag values are ABI.

---

## 1. Scope

**In scope (the BOOPSI core every Release-2 class user touches):**

- **Class lifecycle** — `MakeClass` / `FreeClass` (build/destroy a class),
  `AddClass` / `RemoveClass` (publish/unpublish a named public class).
- **Object lifecycle** — `NewObjectA` (alloc + `OM_NEW`), `DisposeObject`
  (`OM_DISPOSE` + free).
- **Attributes** — `SetAttrsA` (`OM_SET`), `GetAttr` (`OM_GET`),
  `SetGadgetAttrsA` (set + visual refresh), object iteration `NextObject`.
- **Method dispatch** — `DoMethodA` / `DoSuperMethodA` / `CoerceMethodA`
  (and their varargs forms) — note these are **amiga.lib**, not
  intuition LVOs (§2.6).
- **The built-in `rootclass`** — the base every class derives from; its
  dispatcher implements `OM_NEW` (allocate), `OM_DISPOSE` (free),
  `OM_SET`/`OM_GET`/`OM_UPDATE` (no-op base), `OM_ADDMEMBER`/
  `OM_REMMEMBER` (instance lists).
- ABI-complete headers: `<intuition/classes.h>`, `<intuition/classusr.h>`
  (object/class structs, `OM_*` method IDs, `opSet`/`opGet`/`opUpdate`/
  `opMember` message shapes, `OCLASS`/`INST_DATA`/`SIZEOF_INSTANCE` macros).

**Out of scope (deferred / later epics):**

- **The concrete built-in system classes** — `gadgetclass`, `imageclass`,
  `frameiclass`, `sysiclass`, `icclass`/`modelclass` (interconnection),
  `propgclass`, `strgclass`, `buttongclass`, etc. These are what gadtools
  (L8) and asl (L9) instantiate; **L7 ships the machinery + `rootclass`
  only**, with the first concrete class (`gadgetclass`) arriving as the
  L8 prerequisite (see §5). A custom test class proves the core.
- **`DoGadgetMethodA`** (the gadget-method router) — L8 with gadgetclass.
- **`OM_NOTIFY` / icclass notification graph** — needs `modelclass` +
  `icclass`; deferred to the gadtools/model work.
- **Class `cl_ObjectCount` / `cl_SubclassCount` reaping, `FreeClass`
  refusing while in use beyond a simple count** — v0 keeps a count and
  refuses `FreeClass` if non-zero, no reaper.
- **BOOPSI images rendered through `DrawImageState`** — depends on
  imageclass + the deferred L5 `DrawImage`.

**Done-bar for L7:** a V36 Gleas `MakeClass`es a subclass of `rootclass`
with its own dispatcher (handling `OM_NEW`/`OM_SET`/`OM_GET` for one
attribute), `NewObject`s an instance, `SetAttrs` + `GetAttr` round-trip
the attribute, `DoMethod` invokes a custom method, an unknown attr/method
falls through to `rootclass` via `DoSuperMethod`, and `DisposeObject`
frees it — plus `AddClass`/`FindClass`/`RemoveClass` round-trip a public
named class so `NewObject("name", ...)` resolves. ABI-complete
declaration + stub coverage for the rest.

---

## 2. The key decisions

### 2.1 Object & class memory model (verbatim BOOPSI)

An object is an instance-data block preceded by a hidden header. The
public ABI (`<intuition/classes.h>`):

```c
struct _Object {            // the hidden prefix
    struct MinNode o_Node;  // links instances on a class/parent list
    struct IClass *o_Class; // the object's class — OCLASS(o)
};

struct IClass {
    struct Hook   cl_Dispatcher; // h_Entry(cl, obj, msg) — the method fn
    ULONG         cl_Reserved;
    struct IClass *cl_Super;     // superclass (rootclass at the top)
    ClassID       cl_ID;         // public name, or nullptr (private)
    UWORD         cl_InstOffset; // where this class's instance data starts
    UWORD         cl_InstSize;   // size of this class's own instance data
    APTR          cl_UserData;
    ULONG         cl_SubclassCount;
    ULONG         cl_ObjectCount;
    ULONG         cl_Flags;
};
```

`NewObjectA` returns a pointer to the instance data (`Object *`), **not**
to `_Object`. The header is recovered by `((struct _Object *)obj) - 1`.
Canonical macros (verbatim): `OCLASS(o)`, `INST_DATA(cl,o)`
(`= (BYTE*)o + cl->cl_InstOffset`), `SIZEOF_INSTANCE(cl)`,
`BASEOBJECT(o)`. `cl_InstOffset` accumulates down the superclass chain:
rootclass owns `[0, rootInst)`, a subclass owns
`[super->cl_InstOffset+super->cl_InstSize, …)`. `MakeClass` computes a
subclass's `cl_InstOffset = super->cl_InstOffset + super->cl_InstSize`
and total object size = `sizeof(_Object) + cl_InstOffset + cl_InstSize`.

**All objects and IClass structs live in the SASOS shared heap**
(`Croi_AllocShared`) — they are passed between the class author's U-mode
code, the dispatcher, and (for gadgets, L8) the kernel-side Leargas
renderer, so every party must be able to read them.

### 2.2 Dispatch is **local** (U-mode), driven by U-mode hooks

A class's `cl_Dispatcher` is a `struct Hook` whose `h_Entry` is the class
author's own code — arbitrary **U-mode** function. Invoking it is a plain
in-process C call. S-mode can never call it (the L1 RX-page lesson: the
kernel can't fetch user pages for execution; and the dispatcher is
untrusted user code). **Therefore the entire dispatch + object-lifecycle
path runs in U-mode**, as `local`-flavour LVOs in the
`.lib_text.intuition` RX page (the L2 utility / L5 requester pattern):

- `NewObjectA`, `DisposeObject`, `SetAttrsA`, `GetAttr`,
  `SetGadgetAttrsA`, `NextObject`, `MakeClass`, `FreeClass` → **`local`**.
- They allocate via an **inlined `AllocVec`/`FreeVec` ecall**
  (`SYS_AllocVec`/`SYS_FreeVec`, the marshalling-stub macro from
  `graphics_blit.c` / `req_stubs.c`) — never an out-of-section call
  (PC-rel overflow). Object memory comes from a shared-heap AllocVec.
- They walk tag lists with the L2 utility walkers, which are themselves
  `local` U-mode code in `.lib_text.utility` — reachable from U-mode.

### 2.3 The built-in `rootclass` dispatcher

`rootclass` is CaraOS-authored and lives in `.lib_text.intuition` (U-mode
local code), so subclass dispatchers reach it by `DoSuperMethod`. Its
dispatcher handles:

- `OM_NEW` — allocate `sizeof(_Object) + cl->cl_InstOffset +
  cl->cl_InstSize` from the shared heap (inlined `AllocVec`, cleared),
  fill `_Object.o_Class`, init `o_Node`, return the `Object *`. (In real
  BOOPSI the *true* class's `OM_NEW` arrives at rootclass via
  `DoSuperMethodA` from the subclass; rootclass does the allocation using
  the **true class** carried in the `opSet`/`opNew` — CaraOS passes the
  true class through so the right total size is allocated.)
- `OM_DISPOSE` — `FreeVec` the object (inlined `SYS_FreeVec`).
- `OM_SET` / `OM_UPDATE` — base no-op, returns 0 (subclasses handle their
  own attrs and `DoSuperMethod` the rest here).
- `OM_GET` — base no-op, `*storage = 0`, returns 0.
- `OM_ADDMEMBER` / `OM_REMMEMBER` — link/unlink the member on the
  object's `o_Node` list (used by model/list objects).

`rootclass` is itself a `struct IClass` constructed once at boot
(`entry.c`, alongside the other library bases) with `cl_Super = nullptr`,
`cl_ID = "rootclass"`, `cl_InstOffset = 0`, `cl_InstSize =
sizeof(rootclass instance)` (v0: 0 — rootclass holds no own data beyond
the header), and `cl_Dispatcher.h_Entry` = the built-in dispatcher in the
RX page. It is registered in the public class registry (§2.5) so
`MakeClass(…, "rootclass", nullptr, …)` resolves the superclass by name.

### 2.4 `MakeClass` / `FreeClass`

`MakeClass(classID, superID, superPtr, instSize, flags)`:
- Resolve the superclass: `superPtr` if given, else `FindClass(superID)`
  (inlined `SYS_FindClass` ecall) — defaults to `rootclass`.
- `AllocVec` a `struct IClass` (shared heap), set `cl_Super`,
  `cl_ID = classID` (may be nullptr → private class), `cl_InstSize =
  instSize`, `cl_InstOffset = super->cl_InstOffset + super->cl_InstSize`,
  bump `super->cl_SubclassCount`.
- The caller fills `cl_Dispatcher` + `cl_UserData` after the call (the
  V36 idiom), or CaraOS accepts them via tags later. v0: caller sets
  `cl->cl_Dispatcher` directly (it's a public field).
- `FreeClass` refuses (returns FALSE) if `cl_ObjectCount` or
  `cl_SubclassCount` is non-zero; else decrements the super's
  `cl_SubclassCount` and `FreeVec`s the IClass. (`AddClass` must not have
  it published — caller `RemoveClass` first; v0 just checks the registry.)

### 2.5 Public class registry — **kernel registry, `syscall`**

Named public classes (`AddClass`) must be findable by name from any task
(`NewObject(nullptr, "buttongclass", …)`). Mirror the L6 device registry:
a small kernel table keyed by `ClassID` string.

- `AddClass(cl)` → register `cl->cl_ID → cl` (skip duplicate names, like
  the device registry).
- `RemoveClass(cl)` → unregister.
- `FindClass(classID)` → look up, return `IClass *` (or nullptr).

These three are **`syscall`** (`Croi_AddClass_Impl` / `RemoveClass` /
`FindClass`). The IClass structs themselves live in the shared heap
(built by the U-mode `MakeClass`); the registry only stores pointers, and
the kernel reads `cl_ID` via SUM=1. `rootclass` is pre-registered at boot.
`NewObjectA(classPtr, classID, …)`: if `classPtr` is given use it, else
`FindClass(classID)` via inlined ecall, then run `OM_NEW` locally.

> Why a kernel registry and not a shared-heap list managed in U-mode:
> the registry is global cross-task state; a kernel table is the same
> robust, lock-free-by-cooperative-scheduling pattern L6 already proved,
> and the 3 calls are cold-path (class setup, not per-object).

### 2.6 `DoMethod` / `DoSuperMethod` / `CoerceMethod` — **libcara**, not LVOs

In AmigaOS these are **amiga.lib** functions (varargs), *not*
`intuition.library` LVOs — a V36 program links them from amiga.lib. CaraOS
has no amiga.lib yet, but it has **libcara** (the U-mode support lib that
already supplies `memset`/`memcpy` and `SysBase`). Decision: put the
dispatch helpers there, with verbatim names, declared in a new verbatim
`<clib/alib_protos.h>`:

- `DoMethodA(Object *o, Msg msg)` → `cl = OCLASS(o);
  cl->cl_Dispatcher.h_Entry(cl, o, msg)`.
- `DoSuperMethodA(Class *cl, Object *o, Msg msg)` →
  `cl->cl_Super->cl_Dispatcher.h_Entry(cl->cl_Super, o, msg)`.
- `CoerceMethodA(Class *cl, Object *o, Msg msg)` →
  `cl->cl_Dispatcher.h_Entry(cl, o, msg)` (dispatch *as* an explicit
  class).
- Varargs `DoMethod`/`DoSuperMethod`/`CoerceMethod` pack `ULONG MethodID`
  + following args into a small stack buffer and call the `…A` form. The
  first word of every method message *is* the MethodID, so a packed
  `{MethodID, arg1, arg2, …}` is a valid `Msg` (the BOOPSI contract).

Pure in-process pointer-chasing — no syscall, no library base. They need
only the public `_Object`/`IClass` layout (§2.1).

### 2.7 Method & attribute message shapes (verbatim)

`<intuition/classusr.h>`: `typedef struct IClass *Class; typedef APTR
Object;` `typedef struct { ULONG MethodID; } *Msg;`. Method IDs:
`OM_NEW`(0x101)/`OM_DISPOSE`/`OM_SET`/`OM_GET`/`OM_ADDTAIL`/`OM_REMOVE`/
`OM_NOTIFY`/`OM_UPDATE`/`OM_ADDMEMBER`/`OM_REMMEMBER` — verbatim values.
Messages: `struct opSet { ULONG MethodID; struct TagItem *ops_AttrList;
struct GadgetInfo *ops_GInfo; }` (OM_NEW/OM_SET share this), `struct
opGet { ULONG MethodID; ULONG opg_AttrID; ULONG *opg_Storage; }`, `struct
opUpdate`, `struct opMember`. All read by dispatchers via SUM=1 where the
kernel is involved (L8 gadgets); pure U-mode otherwise.

---

## 3. LVO offsets (canonical — **verify at L7.1**)

The conf currently ends at `OpenScreenTagList -612` (ord 101). BOOPSI
slots in after a pad. Canonical `intuition_lib.fd` offsets (confirm each
against `amiga_docs/` RKM/FD before declaring — offsets are ABI and feed
Phase 9):

| LVO | offset | flavour | slice |
|-----|--------|---------|-------|
| `NewObjectA`      | -636 | local   | L7.1 |
| `DisposeObject`   | -642 | local   | L7.1 |
| `SetAttrsA`       | -648 | local   | L7.2 |
| `GetAttr`         | -654 | local   | L7.2 |
| `SetGadgetAttrsA` | -660 | local   | L7.2 |
| `NextObject`      | -666 | local   | L7.2 |
| `FindClass`       | -672 | syscall | L7.3 |
| `MakeClass`       | -678 | local   | L7.1 |
| `AddClass`        | -684 | syscall | L7.3 |
| `RemoveClass`     | -708 | syscall | L7.3 |
| `FreeClass`       | -714 | local   | L7.1 |

(`FindClass` is private in AmigaOS; CaraOS exposes it as the registry
read. The -690..-702 region holds non-BOOPSI V36 calls — `GetDefaultPubScreen`,
`GetScreenDrawInfo`, etc. — leave as `##pad_run` and split later when
those land. Adjust the table to whatever the FD actually says.)

`MakeClass` (5 args) and `SetGadgetAttrsA` (4 args) are within the
7-register budget *as syscalls*, but they're `local` anyway (they touch
U-mode dispatchers / do U-mode allocation), so the register limit is moot
— a local stub takes args in the normal C ABI.

---

## 4. Slice plan

- **L7.1 — class/object core + rootclass.** Headers
  `<intuition/classes.h>` + `<intuition/classusr.h>` (structs, `OM_*`,
  message shapes, macros) + `<clib/alib_protos.h>` (dispatch helpers).
  Built-in `rootclass` dispatcher in `.lib_text.intuition`; constructed +
  registered at boot (entry.c). `MakeClass`/`FreeClass` (local),
  `NewObjectA`/`DisposeObject` (local, drive `OM_NEW`/`OM_DISPOSE`).
  `DoMethodA`/`DoSuperMethodA`/`CoerceMethodA` + varargs in libcara.
  **Test (Gleas):** a custom subclass of rootclass with a dispatcher that
  handles a custom method; `NewObject` → `DoMethod` → `DisposeObject`.
- **L7.2 — attributes + object lists.** `SetAttrsA`/`GetAttr`
  (`OM_SET`/`OM_GET`), `SetGadgetAttrsA` (set + `RefreshGList` when a
  window/gadget is supplied), `NextObject` + `OM_ADDMEMBER`/`OM_REMMEMBER`.
  rootclass `OM_SET`/`OM_GET`/member-list bodies. **Test:** subclass with
  one attribute — `SetAttrs`/`GetAttr` round-trip; an unknown attr falls
  through to rootclass via `DoSuperMethod` and is ignored cleanly.
- **L7.3 — public class registry.** `AddClass`/`RemoveClass`/`FindClass`
  (syscall, kernel registry like devices), `NewObject`-by-name resolution
  in `NewObjectA`. **Test:** `MakeClass` a named public class +
  `AddClass`; `NewObject(nullptr, "name", …)` resolves via `FindClass`;
  `RemoveClass`; `FreeClass` refuses while objects live.

(The first concrete system class — **`gadgetclass`** — is the **L8
gadtools** prerequisite, not L7: it bridges BOOPSI objects to the Leargas
`struct Gadget` substrate and the kernel renderer, and only makes sense
alongside gadtools' button/checkbox/etc. So L7 closes on the machinery +
rootclass + a custom test class. `imageclass`/`icclass`/`modelclass`
follow with their consumers.)

---

## 5. Testing

Dispatch + alloc run in U-mode (`.lib_text.intuition` + inlined ecalls),
so the **kernel self-tests cannot exercise the local stubs directly** (the
S-mode runner can't fetch the User RX page — the standing L1 rule). Test
from a **Gleas**: extend `userintuition.c` (or a new `userboopsi.c`) to
open `intuition.library`, define a custom class in the program's own text
(its dispatcher is ordinary U-mode code), and drive
`MakeClass`/`NewObject`/`SetAttrs`/`GetAttr`/`DoMethod`/`DisposeObject`/
`AddClass`/`FindClass`/`RemoveClass`, returning non-zero on any mismatch
(the `v36hello`/`userexec` exit-code idiom). The **syscall** parts
(`AddClass`/`RemoveClass`/`FindClass` registry) *can* additionally get a
thin `KERNEL_TEST` against `Croi_*_Impl` with synthetic IClass structs in
the shared heap (like `device_io`), since those run in S-mode.

---

## 6. Tracked gaps / deferrals

- Concrete system classes (`gadgetclass`, `imageclass`, `frameiclass`,
  `sysiclass`, `propgclass`, `strgclass`, `buttongclass`, `groupgclass`,
  `icclass`, `modelclass`) — arrive with gadtools (L8) / asl (L9).
- `DoGadgetMethodA`, `OM_NOTIFY`/icclass notification graph — L8.
- `FreeClass` reaping / object enumeration for cleanup — v0 count-only.
- BOOPSI image rendering (`DrawImageState`/imageclass) — gated on the
  deferred L5 `DrawImage` (planar→chunky decode).
- amiga.lib proper — CaraOS folds the handful of needed amiga.lib
  functions into **libcara** under verbatim names + `<clib/alib_protos.h>`
  as they're needed (here: the dispatch helpers); a full amiga.lib is its
  own later concern.
