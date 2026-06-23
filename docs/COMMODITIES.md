# L13 — commodities.library (scope)

Cleanroom V36+ `commodities.library` for CaraOS Phase 3. Commodities is
AmigaOS's framework for input-handling tools: a program (a "commodity")
builds a tree of **CxObj**s — a broker at the root, with filters, type
filters, translators, senders, signallers, and custom objects attached —
and the commodities **input handler**, inserted in the `input.device`
handler chain, runs every `InputEvent` through that tree before intuition
sees it, routing matches back to the commodity's MsgPort as **CxMsg**s.

Two facts shape this epic:

1. **No representative app needs it.** `docs/PHASE3.md §3`'s yardstick apps
   — editor, paint, file-manager — list `dos`/`intuition`/`graphics`/`asl`/
   `gadtools`/`utility`/`iffparse`/`icon`/`diskfont` between them; **none
   lists `commodities`**. By the apps-driven policy (§4, "ABI-complete;
   impl only what an app exercises") commodities has no consumer yet.
2. **Its live half is blocked on deferred substrate.** The `input.device`
   handler chain is *recorded but not invoked* — `IND_ADDHANDLER` stores a
   handler but Leargas drains the input ring directly; intuition is not yet
   an input handler (`docs/CROI_DEVICES.md §2.5`: "the seam a
   commodities.library input handler would need"). So commodities cannot
   actually see live events until that chain lands.

So L13 ships the **app-independent, testable core** — the full ABI, the
CxObj object model, and input-expression parsing — and **defers the live
input dispatch** (the handler-chain hookup, live filtering, CxMsg delivery)
until a commodity program (a future T tool, e.g. a hotkey daemon) forces
the input-handler chain forward. This mirrors how icon/diskfont shipped the
testable core and deferred what needed absent substrate.

Read alongside: `docs/CROI_DEVICES.md §2.5` (input.device + the handler-
chain gap), `docs/DISKFONT.md`/`docs/ICON.md` (scope-doc shape + the
"build the testable core, defer the rest" discipline), `docs/LVO.md`.

---

## 1. Scope

**In scope (gets a working body):**

- The full V36+ `commodities.library` ABI: `include/libraries/
  commodities.h` (CxObj/CxMsg/NewBroker/InputXpression + CX_*/CXM_*/NB_*
  constants) and the `.conf` declaring every canonical LVO.
- The library skeleton (base-ful `syscall` library, the icon/diskfont
  recipe).
- The **CxObj object model** — pure data structure, no live input:
  `CreateCxObj`/`CxBroker`, `AttachCxObj`/`EnqueueCxObj`/`InsertCxObj`/
  `RemoveCxObj` (the broker tree), `ActivateCxObj`, `CxObjType`,
  `CxObjError`/`ClearCxObjError`, `SetCxObjPri`, `DeleteCxObj`/
  `DeleteCxObjAll`.
- Per-object config setters that just *store* their argument (inert until
  live dispatch): `SetFilter`, `SetFilterIX`, `SetTranslate`.
- **`ParseIX`** — parse an input-description string ("ctrl alt f1") into a
  `struct InputXpression`. Pure, testable.
- The **CxMsg accessors**: `CxMsgType`, `CxMsgData`, `CxMsgID`,
  `DisposeCxMsg`, `RouteCxMsg`.

**Out of scope (declared ABI-complete, defined stub):**

- **Live input dispatch** — no `input.device` handler runs the broker
  tree, so filters/translators/senders never see a real `InputEvent` and
  no `CxMsg` is delivered to a commodity's MsgPort. This is the deferred
  half; it lands with the input-handler chain (a future T-tool need).
- **`AddIEvents`** — injecting events into the live input stream (needs
  the active handler chain).
- **`InvertKeyMap`** — depends on a keymap.library that does not exist.
- **`MatchIX`-as-live-filtering** — the IX is parsed and stored; matching a
  live event against it runs only once the handler chain is active.

**Done-bar:** a V36 commodity program compiles, `OpenLibrary`s
commodities.library, `CxBroker`s a broker, `CreateCxObj`s a filter (with a
`ParseIX`'d expression) + a translator, `AttachCxObj`s them, `ActivateCxObj`s
the tree, and `DeleteCxObjAll`s it — the object tree builds, queries
(`CxObjType`/`CxObjError`), and tears down correctly; `ParseIX` populates a
correct `InputXpression`. The library is ABI-complete. Live hotkey
processing is explicitly future work.

---

## 2. The key decisions

### 2.1 Ship the object model; defer the live input flow

Commodities splits cleanly into a **structural half** (build/query/destroy
a CxObj tree, parse input expressions) and a **live half** (the input
handler runs events through the tree and routes CxMsgs). The structural
half is pure data-structure + string-parsing logic — implementable and
fully testable *without* any input plumbing. The live half needs the
`input.device` handler chain, which is deferred substrate. L13 builds the
first and stubs the second, so a commodity program links, opens the
library, and constructs its network today; it just won't receive events
until the chain is wired. This is the honest apps-driven cut for a library
with no current consumer.

### 2.2 CxObj is a kernel-private node behind the opaque public handle

The public `struct CxObj` is opaque (apps only hold `CxObj *`). Internally
each is a `CaraCxObj` on the SASOS shared heap carrying: type (`CX_*`),
priority, error word, an active flag, a `MinNode` for its place in the
parent's child list, the child list head (the broker tree), a back-pointer
to the broker, and a small type-specific union — a `NewBroker` copy +
MsgPort for `CX_BROKER`, the stored `InputXpression` for a filter, the
stored translate `InputEvent` list for `CX_TRANSLATE`, the port+signal for
`CX_SEND`/`CX_SIGNAL`. `CreateCxObj` allocates one; `DeleteCxObjAll` walks
the broker's subtree and frees it. (Same opaque-public/private-impl pattern
as `CaraIff`, `CaraAslReq`, the icon `DiskObject` allocation.)

### 2.3 `syscall` flavour; object tree lives in the shared heap

commodities is base-ful `syscall` flavour (icon/diskfont recipe): each LVO
is a `Cara_Trampoline_Cx_*` that `ecall`s into Croi, routed to
`Croi_Cx_*_Impl`. The CxObj tree is shared-heap so the public `CxObj *` is
valid in the caller. The base global is the verbatim **`CxBase`** (a
`struct Library *`). No live state crosses into the kernel beyond the
allocations (there is no input handler running yet).

### 2.4 `ParseIX` is a self-contained string parser

`ParseIX(description, ix)` tokenises a commodity input description —
qualifiers (`ctrl`/`alt`/`shift`/`lcommand`/…), an optional class
(`rawkey`/`rawmouse`/…), and a key/code — into the `InputXpression` fields
(`ix_Class`, `ix_Code`/`ix_CodeMask`, `ix_Qualifier`/`ix_QualMask`/
`ix_QualSame`). Pure logic over the `IEQUALIFIER_*`/`IECLASS_*` constants
already in `devices/inputevent.h`; no kernel state, fully unit-testable.

---

## 3. LVO surface

Bias 30; reserved slots 0–3 are `local` hooks. **Offsets are the canonical
V36+ `commodities_lib.fd` values, locked against `amiga_docs/` when
`tools/lvo-gen/commodities.conf` is written in L13.1** (cross-check, never
copy; `##pad_run` fills any private gaps).

| LVO group | functions | slice |
|-----------|-----------|-------|
| Object lifecycle | `CreateCxObj`, `CxBroker`, `DeleteCxObj`, `DeleteCxObjAll`, `ActivateCxObj` | L13.1 |
| Tree | `AttachCxObj`, `EnqueueCxObj`, `InsertCxObj`, `RemoveCxObj` | L13.1 |
| Query / config | `CxObjType`, `CxObjError`, `ClearCxObjError`, `SetCxObjPri` | L13.1 |
| CxMsg | `CxMsgType`, `CxMsgData`, `CxMsgID`, `DisposeCxMsg`, `RouteCxMsg` | L13.1 |
| Filter config | `SetFilter`, `SetFilterIX`, `SetTranslate` (store only) | L13.2 |
| Parsing | `ParseIX` | L13.2 |
| **Stub** | `AddIEvents`, `InvertKeyMap` (+ any live-dispatch LVOs) | — |

Everything not implemented is declared at its canonical LVO and emitted as
a defined stub (`Croi_LvoUnimplemented`) so a V36 program links.

---

## 4. Slice plan

### L13.1 — library + the CxObj object model

- `tools/lvo-gen/commodities.conf` (full surface, offsets locked) →
  `proto/commodities.h` / `commodities/lvo.h` / `commodities_vec.c`;
  `include/libraries/commodities.h` (verbatim `CxObj`/`CxMsg`/`NewBroker`/
  `InputXpression` ABI + `CX_*`/`CXM_*`/`NB_VERSION`); the
  `src/croi/commodities` library (base, hooks, trampolines, MakeLibrary in
  `entry.c`, `KEEP(.lib_text.commodities)`, whole-archive, coverage wiring).
- `CreateCxObj`/`CxBroker`/`AttachCxObj`/`EnqueueCxObj`/`InsertCxObj`/
  `RemoveCxObj`/`ActivateCxObj`/`CxObjType`/`CxObjError`/`ClearCxObjError`/
  `SetCxObjPri`/`DeleteCxObj`/`DeleteCxObjAll` over the `CaraCxObj` tree;
  the `CxMsg` accessors (`CxMsgType`/`CxMsgData`/`CxMsgID`/`DisposeCxMsg`/
  `RouteCxMsg`).
- **Test (KERNEL_TEST):** build a broker (`CxBroker`) + a filter + a
  translate object, `AttachCxObj` them, assert `CxObjType` of each,
  `ActivateCxObj`, then `DeleteCxObjAll` — the tree builds + tears down,
  no leaks, error words clear. Pure, no input.

### L13.2 — ParseIX + filter config (closes L13)

- `ParseIX` (string → `InputXpression`), `SetFilterIX`/`SetFilter`/
  `SetTranslate` (store the parsed IX / filter string / translate list on
  the CxObj). `AddIEvents`/`InvertKeyMap` stay stubbed.
- **Test (KERNEL_TEST):** `ParseIX("ctrl alt f1", &ix)` → assert
  `ix_Qualifier` has the ctrl+alt bits and `ix_Code` is the F1 rawkey;
  `SetFilterIX(filterObj, &ix)` stores it (read back via the object).
- Document the deferred live-dispatch half + its trigger (the input-handler
  chain, when a commodity T-tool needs it).

---

## 5. Testing

- **Object model** (L13.1): KERNEL_TEST building/querying/tearing down a
  CxObj tree (no input, no Process needed).
- **ParseIX** (L13.2): KERNEL_TEST (or host unit test if the parser is
  factored shared) asserting the parsed `InputXpression` fields; filter
  config read-back.
- A `userexec`-style Gleas opening commodities.library + building a tree
  via the proto stubs proves the U-mode dispatch path (optional; the
  KERNEL_TESTs cover the logic).

Each slice ends on the standing gate: host `ctest` green, in-kernel runner
`0 failed`, format-check clean, two-boot QEMU smoke `ok`; commit; regen
`docs/LVO_COVERAGE.md`; handoff/memory follow-up.

---

## 6. Tracked gaps / deferrals

- **The input-handler chain** — the load-bearing deferral. commodities'
  whole live purpose (filtering live `InputEvent`s, routing `CxMsg`s to a
  commodity's MsgPort) waits until `input.device` runs a real handler chain
  (intuition + commodities as handlers; `CROI_DEVICES.md §2.5`). Triggered
  by the first commodity T-tool that needs live hotkeys.
- **`AddIEvents`** (inject into the live stream) and **`InvertKeyMap`**
  (needs keymap.library, which does not exist) — stubbed.
- **Live `MatchIX`** filtering, `CX_TYPEFILTER`/`CX_SEND`/`CX_SIGNAL`/
  `CX_CUSTOM` runtime behaviour — the objects exist in the tree but do
  nothing until dispatch is live.
- **`commodities` shell / Exchange-style management** — a userland tool,
  not the library; far-future.
