# `cruth.library` — the Cara-native 3D API

> Status: design draft. No code has been written against it yet.
> Companion to `docs/ROADMAP.md` Phase 4 (the Cruth deliverables sit
> *inside* Phase 4 — see "Sub-phases" below), `docs/PRINCIPLES.md`
> §3.1 (brand-vs-API namespace split — `Cruth` is brand,
> `cruth.library` is API), `docs/LVO.md` (the dispatch model the
> library is built on), and `docs/ARCHITECTURE.md` §5.2 (the
> per-task handle table Cruth resource handles plug into).

---

## Status — 2026-05-08

Design phase. Spec freeze pending implementation. The plan was
agreed and locked on 2026-05-08; this document is the project-tree
home for that plan. Implementation begins with Cruth-C (Sciath
software path) once Phase 1 ships.

---

## Context

Phase 4's success criterion in `docs/ROADMAP.md` says *"3D runs a
non-trivial demo,"* but does not name the API the demo calls.
Phase 4 as written delivers the driver layer — KMS, command-stream
submission, microcode loading, and 2D acceleration glued under
`graphics.library` via the 1993 RTG vector model. This document
fills the gap: it specifies the 3D API surface programmers actually
write against on top of that driver layer.

The audience the API is sized for: indie game programmers and
demoscene coders shipping a small game or a 4 KB intro on real RV2
silicon. Not engine authors, not AAA. The whole API has to fit in a
programmer's head and on a printout.

The hardware honestly:

- Ky X1 = SpaceMiT K1 → **Imagination IMG BXE-2-32** PowerVR
  (B-series), 819 MHz, ~20 GFLOPS.
- Programmable shader hardware. Vendor-supported APIs on Linux are
  GL ES 3.2, Vulkan 1.3, OpenCL 3.0. We are not those, but we
  cannot pretend the hardware is fixed-function either.
- Tile-based deferred renderer (TBDR): cheap blending and
  occlusion, hidden-surface removal in HW, but framebuffer-load /
  store costs dominate if the API encourages mid-frame readback.
- Microcode is a binary blob (the "necessary blob" exception in
  `docs/PRINCIPLES.md` §2). Two candidate Rogue firmwares are
  staged in `firmware/`; bring-up picks the winner — see Cruth-E.

---

## Strategy

**The simplest thing that lets a programmer draw a textured cube.**
Six concrete things:

1. A way to upload a vertex buffer.
2. A way to upload a texture.
3. A way to load a shader pair (vertex + fragment).
4. A way to bake those into a pipeline along with blend / depth /
   cull state.
5. A draw loop: bind the pipeline, bind some resources, issue
   indexed draws.
6. A way to present the resulting framebuffer.

Everything past that — tessellation, geometry shaders, compute,
multi-queue, async transfer, indirect draws, bindless textures — is
post-v0 scope. Each is listed in §"Out of scope for v0" so future-us
doesn't have to argue them again.

**Why a new library, not Warp3D / GL / Vulkan.**

- AmigaOS V36+ has no canonical 3D library — PRINCIPLES.md §3.1's
  verbatim-name rule does not bind. We are free to design.
- Warp3D was Hyperion's 1998 third-party API; we are not bug-compat
  with it and naming this `warp3d.library` would imply we are.
- Vulkan / OpenGL conformance is explicitly out of Phase 4 scope
  per ROADMAP.md.
- The user-facing brief is *simple, elegant, easy to comprehend* —
  the central design constraint is comprehensibility, which neither
  GL nor Vulkan scores on.

**Brand and public name.**

- Brand: **Cruth** (Irish *cruth*, "shape / form"). Files in
  `src/croi/cruth/`, brand-internal headers in `include/cara/cruth/`
  if any.
- Public library name: **`cruth.library`**. Public header at
  `include/cruth/cruth.h` (this is the application-facing API
  namespace, parallel to `<intuition/...>` / `<graphics/...>`).
- Auto-generated stubs in `include/proto/cruth.h` per LVO.md §6.
- Software-fallback brand: **Sciath** (Irish, "shield"). Internal
  only — programmers see one library.

---

## Sub-phases (inside Phase 4)

Eight steps, A–H. A–D run independently of GPU silicon — they can
land on QEMU before BXE-2 bring-up completes.

| Step | Deliverable | Lives in |
|------|-------------|----------|
| **Cruth-A** | This document. | `docs/CRUTH.md` |
| **Cruth-B** | Public header skeleton: handles, descriptor structs, `CRUTH_PT_*` tag IDs, enums. | `include/cruth/cruth.h` |
| **Cruth-B′** | LVO declarative spec: every public function, flavour, ordinal. | `tools/lvo-gen/cruth.conf` |
| **Cruth-C** | Sciath software path: triangle setup, scanline rasteriser, depth test, texture sample, SPIR-V interpreter. | `src/croi/cruth/sciath/` |
| **Cruth-D** | First demo Gleas: spinning lit textured cube. Runs on QEMU via Sciath. Frame-hash compared to a recorded reference. | `src/userland/cube/` |
| **Cruth-E** | BXE-2 bring-up: KMS + command-stream + microcode loader + fences. Provides the back-end Sciath plugs into. | `src/croi/dath/gpu/` |
| **Cruth-F** | SPIR-V → BXE-2 ISA translator on-target. `Cruth_LoadShader` chooses GPU back-end when present. | `src/croi/cruth/spirv/` |
| **Cruth-G** | `cruthc` host-side GLSL → SPIR-V compiler. Cleanroom front-end. | `tools/cruthc/` |
| **Cruth-H** | Demoscene-aimed sample: 4 KB intro template. Validates the size budget on real RV2 silicon. | `src/userland/cruth_intro/` |

A → B → B′ → C → D unblocks all 3D dev on QEMU.
E follows D and unblocks the GPU back-end.
F follows E.
G can land any time after A.
H is the closing demonstration.

The Phase 4 success criterion is met when D runs on QEMU *and* H
runs on RV2 silicon at 60 fps within budget.

---

## Resource model

Six handle kinds. Every public API call reads as English.

| Kind | Description |
|------|-------------|
| **Buffer** | Vertex / index / uniform memory. Distinguished by usage flag at create. |
| **Texture** | 2D in v0; cube and array later. |
| **Sampler** | Filter, wrap, mip mode. |
| **Shader** | A SPIR-V vertex+fragment pair, compiled to BXE-2 microcode (or interpreted by Sciath). |
| **Pipeline** | Shader + vertex layout + blend + depth + cull + topology, baked. Immutable post-construction. |
| **RenderTarget** | Colour ± depth attachments. |

All exposed as `Cruth_Handle` (opaque `u32`) routed through the
per-task handle table from `docs/ARCHITECTURE.md` §5.2. Lifetimes
are explicit: `Cruth_Free(h)`. Internal refcounting exists for
fence tracking but is invisible to the programmer.

A handle's high bits identify the kind; the runtime asserts on
type-mismatched binds. Programs that pass a `Buffer` handle to
`Cruth_BindTexture` get a clear diagnostic, not a crash.

---

## Pipeline object construction

Tag-list construction matching the rest of the OS
(utility.library):

```c
struct CruthVertexLayout layout = {
    .attr_count = 2,
    .attrs = {
        { .location = 0, .format = CRUTH_VFMT_FLOAT3, .offset = 0  },
        { .location = 1, .format = CRUTH_VFMT_FLOAT2, .offset = 12 },
    },
    .stride = 20,
};

Cruth_Handle pipe = Cruth_MakePipeline(
    CRUTH_PT_SHADER,        shader,
    CRUTH_PT_VERTEX_LAYOUT, &layout,
    CRUTH_PT_BLEND,         CRUTH_BLEND_ALPHA,
    CRUTH_PT_DEPTH_TEST,    CRUTH_CMP_LESS,
    CRUTH_PT_DEPTH_WRITE,   true,
    CRUTH_PT_CULL,          CRUTH_CULL_BACK,
    CRUTH_PT_TOPOLOGY,      CRUTH_TOPO_TRIANGLES,
    TAG_END);
```

Pipelines are immutable post-construction. To change blend mode you
make a new pipeline.

**Vertex layout.** Up to 8 attributes per pipeline. Each attribute
declares its shader location, format
(`CRUTH_VFMT_FLOAT{1,2,3,4}` / `CRUTH_VFMT_U8N{2,4}` /
`CRUTH_VFMT_I16{2,4}`), and byte offset within the per-vertex
record. One stride for the whole record (single interleaved buffer
in v0; multi-buffer vertex pulling is post-v0).

**Bind groups.** Positional, fixed: 8 texture slots, 4
uniform-buffer slots, 2 storage-buffer slots (read-only in v0). A
pipeline's shader declares which slots it actually consumes; the
runtime asserts all consumed slots are bound at draw time.

**Tag IDs.** All `CRUTH_PT_*` values come from a stable namespace
starting at TAG_USER + Cruth-allocated base; declared in
`<cruth/cruth.h>` and consumed by `tools/lvo-gen/cruth.conf` as
literal constants, not symbolic.

---

## Draw loop

```c
Cruth_Begin(target);              // implicit clear via target attrs
Cruth_Bind(pipeline);
Cruth_BindBuffer(0, ubo);         // uniform-buffer slot 0
Cruth_BindTexture(0, atlas);      // texture slot 0
Cruth_BindSampler(0, linear_clamp);
Cruth_DrawIndexed(vbuf, ibuf, /*first=*/0, /*count=*/36);
Cruth_End();
Cruth_Present(target);            // swap; tile resolve handled internally
```

No render-pass / subpass exposed. No barriers, no fences, no
semaphores in the v0 surface. The driver fences and tile-resolves
internally on `End` / `Present`.

Begin/End brackets a single render-target submission. A program
that wants two passes (e.g. shadow map then main scene) calls
Begin/End/Begin/End on different targets — no cross-pass
synchronisation primitives are exposed.

**Hot-path locality.** Per LVO.md §5, Begin / Bind* / Draw* / End
are `flavour = local`: they append to a per-task client-side
command buffer kept in library-private state. Only `Cruth_Present`
crosses the library/Gleas seam (one PutMsg per frame). Draw calls
do not pay IPC overhead; the 4 KB intro budget survives the API.

---

## Shader story

Programmers write GLSL 4.50 vertex+fragment, no preprocessor magic,
no SSBOs, no compute, no geometry/tess.

- `cruthc` (host-only initially, `tools/cruthc/`) compiles GLSL →
  SPIR-V. Cleanroom front-end; we use the open SPIR-V spec and emit
  it directly. No glslang dependency.
- On-target loader `Cruth_LoadShader(spirv_blob, len, flags)`
  translates SPIR-V → BXE-2 microcode at load time and caches the
  result. Cruth-F.
- Sciath path: same loader returns a Sciath `ShaderProgram` —
  internal IR walked by the Sciath interpreter.

The SPIR-V → BXE-2 ISA translator is ours; the BXE-2 microcode
runtime is the firmware blob (the documented exception). Per
PRINCIPLES.md §2: we hold the line that *driver* is cleanroom; only
*microcode* is binary.

Subset constraint summary:

- Vertex + fragment stages only.
- `vec2/3/4`, `mat2/3/4`, `int`, `uint`, `float`, `bool`.
- Texture sampling via `texture(sampler2D, vec2)` and friends.
- `gl_Position` write in vertex; one `out vec4` write in fragment.
- Uniform blocks (`layout(set = 0, binding = N) uniform`) and
  combined image samplers.
- No SSBOs, no atomics, no derivatives beyond `dFdx` / `dFdy`, no
  early-fragment-tests pragma, no subgroup ops.

The full subset is documented in `docs/CRUTH_SHADERS.md` (TBD,
lands with Cruth-G).

---

## Sciath — software fallback

Same `cruth.library` API. Driver init detects no BXE-2 → installs
Sciath as the back-end inside the Cruth Gleas.

- Triangle setup + scanline rasteriser + perspective-correct
  attribute interpolation + depth test + nearest / bilinear
  texture sample on a `DathFramebuffer` (see
  `include/cara/dath.h`).
- Shaders run interpreted in v0 — a small SPIR-V interpreter, not a
  JIT. Slow but correct; demoscene authors who need CPU speed can
  drop to `dath` directly for raw pixel work.
- `RenderTarget` wraps a `DathFramebuffer`; `Cruth_Present` for a
  Sciath target is a `Dath_BlitRect` to the active screen's
  framebuffer.
- Brand is internal only: `src/croi/cruth/sciath/`. The runtime
  picks; programmers do not.

**Why Sciath is in v0, not deferred.** QEMU has no PowerVR. All
unit tests, the Cruth-D cube demo, and most daily dev rely on
software rasterisation. Phase 1 already has CPU pixel / line / blit
primitives in `dath`; Sciath builds on those. A shipped software
path also keeps demoscene authors honest — the same
`cruth.library` call works everywhere.

---

## LVO surface (v0 functions, by flavour)

The full canonical LVO table lives in `tools/lvo-gen/cruth.conf`
(Cruth-B′). Summary:

### Resource lifecycle — `flavour = server`

These cross the library / Cruth-Gleas seam: GPU memory allocation,
descriptor-set creation, microcode compile.

| Function | Returns | Purpose |
|----------|---------|---------|
| `Cruth_MakeBuffer(usage, size, init)` | `Handle` | Allocate vertex / index / uniform buffer. |
| `Cruth_MakeTexture(tags)` | `Handle` | Allocate 2D texture; tags carry width/height/format/levels. |
| `Cruth_MakeSampler(tags)` | `Handle` | Filter / wrap / mip-mode descriptor. |
| `Cruth_MakeShader(spirv, len, flags)` | `Handle` | Compile SPIR-V to BXE-2 / Sciath shader program. |
| `Cruth_MakePipeline(tags)` | `Handle` | Bake shader + layout + state into an immutable pipeline. |
| `Cruth_MakeRenderTarget(tags)` | `Handle` | Allocate RT with declared colour ± depth attachments. |
| `Cruth_Free(h)` | `void` | Release any of the above. Idempotent on already-freed. |
| `Cruth_UpdateBuffer(h, offset, src, size)` | `void` | Memcpy + cache-flush into a buffer. |
| `Cruth_UpdateTexture(h, region, src)` | `void` | Texture upload. |

### Draw loop — `flavour = local`

These append commands to a per-task client-side ring kept in
library-private state. No IPC per call.

| Function | Returns | Purpose |
|----------|---------|---------|
| `Cruth_Begin(rt)` | `void` | Open a frame against the given render target. |
| `Cruth_End()` | `void` | Close the current frame. |
| `Cruth_Bind(pipeline)` | `void` | Set the active pipeline. |
| `Cruth_BindBuffer(slot, buf)` | `void` | Bind UBO at slot. |
| `Cruth_BindTexture(slot, tex)` | `void` | Bind texture at slot. |
| `Cruth_BindSampler(slot, samp)` | `void` | Bind sampler at slot. |
| `Cruth_Draw(vbuf, first, count)` | `void` | Non-indexed draw. |
| `Cruth_DrawIndexed(vbuf, ibuf, first, count)` | `void` | Indexed draw. |
| `Cruth_PushConstants(data, size)` | `void` | Up to 128 bytes per pipeline; no other bind needed. |

### Submission — `flavour = server`

| Function | Returns | Purpose |
|----------|---------|---------|
| `Cruth_Present(rt)` | `void` | Flush the client-side ring to the Cruth Gleas; vsync-swap when the RT is the active screen. |

### Reserved (per LVO.md §3)

LIB_OPEN, LIB_CLOSE, LIB_EXPUNGE, LIB_EXTFUNC at runtime ordinals
0–3; canonical V36+ LVOs at -6, -12, -18, -24. Implementations
follow the standard `Croi_MakeLibrary`-time hook contract.

Total user-defined LVO count in v0: 19. Canonical LVO range
-30 … -138.

---

## Library-private state

Per `docs/LVO.md` §3 the library carries a positive-side
private-state region past the public `struct Library`. For
`cruth.library`:

```c
struct CruthBase {
    struct Library lib;          // V36+ public prefix

    // Per-task state — Cruth API is one-context-per-task in v0;
    // the cmd ring lives in the *task's* lower-half data, not here.
    // This struct is shared library state only.

    Handle gleas_port_kobj;      // server MsgPort Kobj id (Cruth Gleas)
    u32    capabilities;         // bit 0: GPU-backed; bit 1: Sciath-backed; both possible
    u32    backend_chosen;       // 0 = Sciath, 1 = GPU
    u8     fw_sha256[32];        // microcode SHA-256 captured at load
    // ...
};
```

The per-task client-side command ring lives in the calling task's
private memory, allocated by libcara at task startup, recorded in
the task's `tc_UserData` slot or equivalent (TBD with Phase 1
runtime finalisation). The local-flavoured stubs read it from
there.

---

## What's out of v0

Each is bounded; each can land later as an extension; none is in v0
success.

- Tessellation / geometry / mesh shaders.
- Compute shaders. (Phase 4 ROADMAP already excludes compute.)
- Multi-queue / async transfer / explicit barriers / explicit
  semaphores.
- Bindless textures / descriptor indexing.
- Indirect draws.
- Multiple subpasses inside one render pass.
- Sparse / virtual textures.
- Multi-sample beyond 4×.
- Conservative rasterisation, ROVs, fragment interlocks.
- Push constants beyond 128 bytes.
- Hot-reload of shaders. (Phase 6+ ergonomics question.)
- Multi-display / multi-context.
- Audio-sync hook. (Phase 5's `ceol.device` lands the timing
  primitive; cruth.library v0 is rendering-only.)

---

## What this unblocks

- **Phase 4 success criterion** ("3D runs a non-trivial demo") is
  verifiable on QEMU once Cruth-A through Cruth-D land, before the
  GPU bring-up finishes.
- **Phase 8 Deluxe Paint for 2026.** ROADMAP.md Phase 8 Subgoal 8
  says "brush dabs and layer composites run on the Phase 4 GPU
  when present; CPU rasteriser otherwise." Cruth is what that
  means concretely.
- **Phase 9 binary translator** does *not* depend on Cruth — 68k
  programs that wanted 3D used Warp3D, which CaraOS is not. Phase
  9 LVO remap stops at the V36+ canonical surface.
- **Demoscene viability.** A 4 KB intro template against
  cruth.library validates that the API surface costs nothing the
  size budget can't absorb.

---

## Verification

End-to-end testability without RV2 silicon:

- **Host unit tests** (Cruth-A/B/B′/C): pipeline-object
  construction, vertex layout validation, tag-list parsing,
  scanline rasteriser correctness against golden images, SPIR-V
  interpreter against a small set of known-correct shaders.
- **QEMU smoke test** (Cruth-D): Splanc → Croi → Cruth (Sciath) →
  spinning textured cube on the QEMU display, frame-hash checked
  against a recorded reference. This is the test that proves 3D
  works end-to-end without RV2 hardware.
- **RV2 silicon** (Cruth-E onward): the same Cube Gleas binary
  auto-selects the GPU back-end; FPS measured against Sciath;
  visual diff between the two paths within tolerance.
- **Demoscene viability** (Cruth-H): the 4 KB intro template links
  against `cruth.library`, fits the budget, and runs at 60 fps on
  RV2 silicon.

---

## See also

- `docs/ROADMAP.md` Phase 4 — the parent phase. The Cruth deliverables
  sit inside Phase 4's success criterion.
- `docs/PRINCIPLES.md` §2 — the GPU microcode blob exception that
  covers the BXE-2 firmware Cruth loads. §3.1 — why cruth.library is
  a new name, not warp3d.library or graphics3d.library.
- `docs/ARCHITECTURE.md` §5.2 — the per-task handle table
  `Cruth_Handle` plugs into.
- `docs/LVO.md` — the dispatch model. `tools/lvo-gen/cruth.conf` is
  the declarative spec; the inline stubs in `<proto/cruth.h>` are
  auto-generated.
- `include/cara/dath.h` — the framebuffer abstraction Sciath
  rasterises into; `RenderTarget` wraps a `DathFramebuffer`.
- `include/utility/tagitem.h` — the tag-list shape Cruth descriptors
  use.
- `firmware/rogue_36.29.52.182_v1.fw` and
  `firmware/rogue_36.53.104.796_v1.fw` — both staged for Cruth-E
  bring-up; A/B test picks the keeper.
- `amigaos_kb_markdown/International_Amiga_Developers_Conference_Notes_1993_Commodore.md`
  RTG section — the existing Phase 4 reference; Cruth's GPU
  back-end (Cruth-E/F) hooks in via this same vector model,
  alongside the existing 2D RTG glue.
