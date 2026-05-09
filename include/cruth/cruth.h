// SPDX-License-Identifier: BSD-2-Clause
//
// cruth.library — the Cara-native 3D API.
//
// Phase 4 deliverable; designed for indie game programmers and demoscene
// coders. See docs/CRUTH.md for the full design spec, including the
// Sub-phases (Cruth-A through Cruth-H) the implementation lands in.
//
// This header is the application-facing API namespace (parallel to
// <intuition/intuition.h>, <graphics/gfx.h>, etc.). It declares public
// types, descriptor structs, and CRUTH_PT_* tag IDs only — function
// prototypes come from auto-generated <proto/cruth.h> driven by
// tools/lvo-gen/cruth.conf, per docs/LVO.md §6.
//
// The library has no AmigaOS V36+ canonical analogue; per
// docs/PRINCIPLES.md §3.1 the verbatim-name rule does not apply, and
// Cara-native names are used throughout. The internal implementation
// brand is "Cruth" (Irish, "shape / form"); programmers see only the
// public cruth.library surface.

#ifndef CRUTH_CRUTH_H
#define CRUTH_CRUTH_H

#include <exec/libraries.h>
#include <exec/types.h>
#include <utility/tagitem.h>

// ---- Library base ---------------------------------------------------------
//
// Returned by OpenLibrary("cruth.library", 0) and assigned to the
// program's `struct CruthBase *CruthBase;` global. V36+ idiom:
// programs reach the library by dereferencing this pointer; the
// proto/cruth.h inline stubs read the negative-side LVO table from
// it (see docs/LVO.md §6).

struct CruthBase {
    struct Library lib;        // V36+ public prefix; lib_Version etc.
    ULONG          cb_Capabilities; // bit 0: GPU-backed; bit 1: Sciath-backed
    ULONG          cb_Backend;      // 0 = Sciath, 1 = GPU; runtime-chosen
    ULONG          cb_PadReserved[6];
};

// V36+ source-compat: programs declare `struct CruthBase *CruthBase;`
// as their library base global. Phase 3 libcara startup discovers and
// assigns it; until then, OpenLibrary returns it explicitly.

// ---- Opaque resource handle ----------------------------------------------
//
// Every resource the program creates is a Cruth_Handle. The runtime
// dispatches via the per-task handle table from
// docs/ARCHITECTURE.md §5.2; type-mismatched binds (e.g. a Buffer
// passed to BindTexture) produce a clear diagnostic, not a crash.
//
// CRUTH_HANDLE_NULL is the canonical "no resource" value, returned by
// failed Make* calls and accepted by Cruth_Free as a no-op.

typedef ULONG Cruth_Handle;

#define CRUTH_HANDLE_NULL ((Cruth_Handle)0)

typedef enum : UWORD {
    CRUTH_KIND_NONE         = 0,
    CRUTH_KIND_BUFFER       = 1,
    CRUTH_KIND_TEXTURE      = 2,
    CRUTH_KIND_SAMPLER      = 3,
    CRUTH_KIND_SHADER       = 4,
    CRUTH_KIND_PIPELINE     = 5,
    CRUTH_KIND_RENDERTARGET = 6,
} CruthHandleKind;

// ---- Slot and size limits -------------------------------------------------

#define CRUTH_MAX_VERTEX_ATTRS    8  // attrs per pipeline
#define CRUTH_MAX_TEXTURE_SLOTS   8  // bind slots per pipeline
#define CRUTH_MAX_UNIFORM_SLOTS   4  // uniform-buffer bind slots
#define CRUTH_MAX_STORAGE_SLOTS   2  // storage-buffer bind slots (RO in v0)
#define CRUTH_MAX_PUSH_CONST     128 // bytes per pipeline

// ---- Enums ----------------------------------------------------------------

typedef enum : UWORD {
    CRUTH_VFMT_NONE   = 0,
    CRUTH_VFMT_FLOAT1 = 1,
    CRUTH_VFMT_FLOAT2 = 2,
    CRUTH_VFMT_FLOAT3 = 3,
    CRUTH_VFMT_FLOAT4 = 4,
    CRUTH_VFMT_U8N2   = 5, // unsigned 8-bit normalised, 2 components
    CRUTH_VFMT_U8N4   = 6,
    CRUTH_VFMT_I16N2  = 7, // signed 16-bit normalised
    CRUTH_VFMT_I16N4  = 8,
    CRUTH_VFMT_U16N2  = 9,
    CRUTH_VFMT_U16N4  = 10,
} CruthVertexFormat;

typedef enum : UWORD {
    CRUTH_TOPO_TRIANGLES      = 0,
    CRUTH_TOPO_TRIANGLE_STRIP = 1,
    CRUTH_TOPO_LINES          = 2,
    CRUTH_TOPO_LINE_STRIP     = 3,
    CRUTH_TOPO_POINTS         = 4,
} CruthTopology;

typedef enum : UWORD {
    CRUTH_CMP_NEVER    = 0,
    CRUTH_CMP_LESS     = 1,
    CRUTH_CMP_EQUAL    = 2,
    CRUTH_CMP_LEQUAL   = 3,
    CRUTH_CMP_GREATER  = 4,
    CRUTH_CMP_NEQUAL   = 5,
    CRUTH_CMP_GEQUAL   = 6,
    CRUTH_CMP_ALWAYS   = 7,
} CruthCompareOp;

typedef enum : UWORD {
    CRUTH_CULL_NONE  = 0,
    CRUTH_CULL_BACK  = 1,
    CRUTH_CULL_FRONT = 2,
} CruthCullMode;

typedef enum : UWORD {
    CRUTH_BLEND_NONE      = 0, // opaque
    CRUTH_BLEND_ALPHA     = 1, // src.rgb * src.a + dst.rgb * (1 - src.a)
    CRUTH_BLEND_ADDITIVE  = 2, // src + dst, clamped
    CRUTH_BLEND_PREMUL    = 3, // src + dst * (1 - src.a) — premultiplied
    CRUTH_BLEND_MULTIPLY  = 4, // src * dst
} CruthBlendMode;

typedef enum : UWORD {
    CRUTH_TFMT_RGBA8       = 0, // 32-bit RGBA, 8 bits per channel — primary
    CRUTH_TFMT_RGBA8_SRGB  = 1, // sRGB-encoded RGBA8
    CRUTH_TFMT_R8          = 2, // single-channel 8-bit (masks, fonts)
    CRUTH_TFMT_RG8         = 3, // two-channel 8-bit
    CRUTH_TFMT_RGB565      = 4, // 16-bit RGB
    CRUTH_TFMT_DEPTH16     = 5, // 16-bit depth (no stencil in v0)
    CRUTH_TFMT_DEPTH24     = 6, // 24-bit depth, padded
} CruthTextureFormat;

typedef enum : UWORD {
    CRUTH_FILTER_NEAREST = 0,
    CRUTH_FILTER_LINEAR  = 1,
} CruthFilterMode;

typedef enum : UWORD {
    CRUTH_WRAP_REPEAT      = 0,
    CRUTH_WRAP_CLAMP_EDGE  = 1,
    CRUTH_WRAP_MIRROR      = 2,
} CruthWrapMode;

typedef enum : UWORD {
    CRUTH_MIP_NONE    = 0, // no mips
    CRUTH_MIP_NEAREST = 1, // pick nearest level
    CRUTH_MIP_LINEAR  = 2, // trilinear
} CruthMipMode;

// Buffer usage. Pass to Cruth_MakeBuffer. May combine STORAGE with
// VERTEX or INDEX where the hardware supports it; the runtime
// validates.
typedef enum : ULONG {
    CRUTH_BUF_VERTEX  = 1u << 0,
    CRUTH_BUF_INDEX   = 1u << 1,
    CRUTH_BUF_UNIFORM = 1u << 2,
    CRUTH_BUF_STORAGE = 1u << 3, // read-only storage in v0
    CRUTH_BUF_DYNAMIC = 1u << 4, // hint: frequent UpdateBuffer
} CruthBufferUsage;

// ---- Vertex layout descriptor --------------------------------------------
//
// A small flat struct passed by pointer through the CRUTH_PT_VERTEX_LAYOUT
// pipeline tag. All offsets are bytes within one interleaved per-vertex
// record; v0 supports a single bound vertex buffer per draw.

struct CruthVertexAttribute {
    UWORD             location; // shader location
    CruthVertexFormat format;
    UWORD             offset;   // bytes within the vertex record
    UWORD             pad;
};

struct CruthVertexLayout {
    UWORD                       attr_count; // <= CRUTH_MAX_VERTEX_ATTRS
    UWORD                       stride;     // bytes per vertex
    struct CruthVertexAttribute attrs[CRUTH_MAX_VERTEX_ATTRS];
};

// ---- Texture-update region ------------------------------------------------

struct CruthTextureRegion {
    UWORD x, y;       // origin in texels
    UWORD w, h;       // extent in texels
    UWORD level;      // mip level
    UWORD pad;
    ULONG src_stride; // source bytes per row
};

// ---- Tag IDs --------------------------------------------------------------
//
// CaraOS-native tag-ID space within TAG_USER. Cruth allocates a 4-KiB
// region; each subsystem gets a 256-tag block. Stable across versions.

#define CRUTH_TAG_BASE ((Tag)(TAG_USER + 0x10000))

// MakePipeline tags (PT = "Pipeline Tag")
#define CRUTH_PT_BASE         (CRUTH_TAG_BASE + 0x000)
#define CRUTH_PT_SHADER        (CRUTH_PT_BASE + 0x01) // ti_Data: Cruth_Handle
#define CRUTH_PT_VERTEX_LAYOUT (CRUTH_PT_BASE + 0x02) // ti_Data: struct CruthVertexLayout *
#define CRUTH_PT_BLEND         (CRUTH_PT_BASE + 0x03) // ti_Data: CruthBlendMode
#define CRUTH_PT_DEPTH_TEST    (CRUTH_PT_BASE + 0x04) // ti_Data: CruthCompareOp
#define CRUTH_PT_DEPTH_WRITE   (CRUTH_PT_BASE + 0x05) // ti_Data: BOOL
#define CRUTH_PT_CULL          (CRUTH_PT_BASE + 0x06) // ti_Data: CruthCullMode
#define CRUTH_PT_TOPOLOGY      (CRUTH_PT_BASE + 0x07) // ti_Data: CruthTopology
#define CRUTH_PT_COLOR_FORMAT  (CRUTH_PT_BASE + 0x08) // ti_Data: CruthTextureFormat
#define CRUTH_PT_DEPTH_FORMAT  (CRUTH_PT_BASE + 0x09) // ti_Data: CruthTextureFormat
#define CRUTH_PT_DEBUG_NAME    (CRUTH_PT_BASE + 0x0A) // ti_Data: STRPTR

// MakeTexture tags (TT = "Texture Tag")
#define CRUTH_TT_BASE         (CRUTH_TAG_BASE + 0x100)
#define CRUTH_TT_WIDTH         (CRUTH_TT_BASE + 0x01) // ti_Data: ULONG
#define CRUTH_TT_HEIGHT        (CRUTH_TT_BASE + 0x02)
#define CRUTH_TT_FORMAT        (CRUTH_TT_BASE + 0x03) // ti_Data: CruthTextureFormat
#define CRUTH_TT_LEVELS        (CRUTH_TT_BASE + 0x04) // ti_Data: ULONG; 0 = full chain
#define CRUTH_TT_INIT_DATA     (CRUTH_TT_BASE + 0x05) // ti_Data: APTR (level-0 pixels)
#define CRUTH_TT_INIT_STRIDE   (CRUTH_TT_BASE + 0x06) // ti_Data: ULONG (bytes per row)
#define CRUTH_TT_DEBUG_NAME    (CRUTH_TT_BASE + 0x07)

// MakeSampler tags (ST = "Sampler Tag")
#define CRUTH_ST_BASE         (CRUTH_TAG_BASE + 0x200)
#define CRUTH_ST_FILTER_MIN    (CRUTH_ST_BASE + 0x01) // ti_Data: CruthFilterMode
#define CRUTH_ST_FILTER_MAG    (CRUTH_ST_BASE + 0x02)
#define CRUTH_ST_MIP_MODE      (CRUTH_ST_BASE + 0x03) // ti_Data: CruthMipMode
#define CRUTH_ST_WRAP_U        (CRUTH_ST_BASE + 0x04) // ti_Data: CruthWrapMode
#define CRUTH_ST_WRAP_V        (CRUTH_ST_BASE + 0x05)
#define CRUTH_ST_DEBUG_NAME    (CRUTH_ST_BASE + 0x06)

// MakeRenderTarget tags (RT = "RenderTarget Tag")
#define CRUTH_RT_BASE         (CRUTH_TAG_BASE + 0x300)
#define CRUTH_RT_WIDTH         (CRUTH_RT_BASE + 0x01) // ti_Data: ULONG
#define CRUTH_RT_HEIGHT        (CRUTH_RT_BASE + 0x02)
#define CRUTH_RT_COLOR_FORMAT  (CRUTH_RT_BASE + 0x03) // ti_Data: CruthTextureFormat
#define CRUTH_RT_DEPTH_FORMAT  (CRUTH_RT_BASE + 0x04) // ti_Data: CruthTextureFormat or _NONE
#define CRUTH_RT_CLEAR_COLOR   (CRUTH_RT_BASE + 0x05) // ti_Data: ULONG (RGBA8 packed)
#define CRUTH_RT_CLEAR_DEPTH   (CRUTH_RT_BASE + 0x06) // ti_Data: float bits, IPTR-cast
#define CRUTH_RT_PRESENT_TO    (CRUTH_RT_BASE + 0x07) // ti_Data: struct Screen * (or null for offscreen)
#define CRUTH_RT_DEBUG_NAME    (CRUTH_RT_BASE + 0x08)

// LoadShader flag bits — passed as ULONG; not tag-list driven since
// shader load is the single hot reload-iterable thing in v0.
typedef enum : ULONG {
    CRUTH_SHADER_NONE       = 0,
    CRUTH_SHADER_VALIDATE   = 1u << 0, // run extra SPIR-V validation pass
    CRUTH_SHADER_DEBUG_INFO = 1u << 1, // retain symbol info for diagnostics
} CruthShaderFlags;

// ---- Library version / revision ------------------------------------------
//
// V36+ idiom; programs OpenLibrary with the version they need.
// cruth.library starts at v0.r0 in the design draft; the first
// shipping version (Cruth-D landing on QEMU) bumps to 1.0 per
// docs/CRUTH.md sub-phase verification.

#define CRUTH_LIBRARY_NAME    ((STRPTR)"cruth.library")
#define CRUTH_LIBRARY_VERSION 0
#define CRUTH_LIBRARY_REVISION 0

#endif // CRUTH_CRUTH_H
