// SPDX-License-Identifier: BSD-2-Clause
//
// FDT parser: hand-built blob walk. Builds a minimal but well-formed DTB
// with one root and one child, containing properties of every type the
// parser exposes accessors for, then exercises each accessor.

#include <cara/fdt.h>
#include <cara/types.h>

#include <stdio.h>
#include <string.h>

#define FDT_MAGIC 0xD00DFEEDu
#define BEGIN_NODE 0x00000001u
#define END_NODE   0x00000002u
#define PROP       0x00000003u
#define FDT_END    0x00000009u

static int fail(const char *msg, int code)
{
    fprintf(stderr, "test_fdt_walk: FAIL: %s\n", msg);
    return code;
}

static void put_be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

// Append a 4-byte token to the structure block at *poff.
static void emit_token(u8 *buf, u32 *poff, u32 tok)
{
    put_be32(buf + *poff, tok);
    *poff += 4;
}

static void emit_begin_node(u8 *buf, u32 *poff, const char *name)
{
    emit_token(buf, poff, BEGIN_NODE);
    u32 nlen = (u32)strlen(name) + 1; // include NUL
    memcpy(buf + *poff, name, nlen);
    *poff += nlen;
    while ((*poff & 3u) != 0) {
        buf[(*poff)++] = 0;
    }
}

static void emit_end_node(u8 *buf, u32 *poff)
{
    emit_token(buf, poff, END_NODE);
}

static void emit_prop(u8 *buf, u32 *poff, u32 nameoff, const u8 *data, u32 len)
{
    emit_token(buf, poff, PROP);
    put_be32(buf + *poff, len);
    *poff += 4;
    put_be32(buf + *poff, nameoff);
    *poff += 4;
    memcpy(buf + *poff, data, len);
    *poff += len;
    while ((*poff & 3u) != 0) {
        buf[(*poff)++] = 0;
    }
}

// Append a NUL-terminated string to the strings block; return its offset.
static u32 string_intern(u8 *strings, u32 *plen, const char *s)
{
    u32 off = *plen;
    u32 n = (u32)strlen(s) + 1;
    memcpy(strings + off, s, n);
    *plen += n;
    return off;
}

// Test data shapes.
struct ChildReg {
    u32 base;
    u32 size;
};

int main(void)
{
    static u8 blob[2048] __attribute__((aligned(8)));
    memset(blob, 0, sizeof(blob));

    // ---- Build strings block first (we need offsets to emit props). ----
    static u8 strings[256];
    u32 slen = 0;
    u32 s_compat   = string_intern(strings, &slen, "compatible");
    u32 s_acells   = string_intern(strings, &slen, "#address-cells");
    u32 s_scells   = string_intern(strings, &slen, "#size-cells");
    u32 s_reg      = string_intern(strings, &slen, "reg");
    u32 s_strlist  = string_intern(strings, &slen, "string-list");
    u32 s_u32prop  = string_intern(strings, &slen, "magic");
    u32 s_u64prop  = string_intern(strings, &slen, "magic64");
    u32 s_emptypropdummy = string_intern(strings, &slen, "present");

    // ---- Build structure block. ----
    u32 soff = 0;

    emit_begin_node(blob, &soff, "");
    {
        u8 acells_be[4];
        put_be32(acells_be, 1);
        emit_prop(blob, &soff, s_acells, acells_be, 4);
        u8 scells_be[4];
        put_be32(scells_be, 1);
        emit_prop(blob, &soff, s_scells, scells_be, 4);
        const char *rc = "test,root";
        emit_prop(blob, &soff, s_compat, (const u8 *)rc, (u32)strlen(rc) + 1);

        emit_begin_node(blob, &soff, "child@1000");
        {
            // compatible = "test,child"
            const char *cc = "test,child";
            emit_prop(blob, &soff, s_compat, (const u8 *)cc, (u32)strlen(cc) + 1);

            // reg = <0x1000 0x100>  (1 cell each)
            u8 reg_be[8];
            put_be32(reg_be + 0, 0x1000);
            put_be32(reg_be + 4, 0x100);
            emit_prop(blob, &soff, s_reg, reg_be, 8);

            // string-list = "alpha","beta","gamma" — the implicit final NUL
            // from the string literal terminates the last string; we keep
            // it in the property bytes.
            const u8 sl[] = "alpha\0beta\0gamma";
            emit_prop(blob, &soff, s_strlist, sl, (u32)sizeof(sl));

            // magic = u32 0xCAFEBABE
            u8 m32[4];
            put_be32(m32, 0xCAFEBABEu);
            emit_prop(blob, &soff, s_u32prop, m32, 4);

            // magic64 = u64 0x123456789ABCDEF0
            u8 m64[8];
            put_be32(m64 + 0, 0x12345678u);
            put_be32(m64 + 4, 0x9ABCDEF0u);
            emit_prop(blob, &soff, s_u64prop, m64, 8);

            // present = (zero-length) — boolean property idiom
            emit_prop(blob, &soff, s_emptypropdummy, nullptr, 0);
        }
        emit_end_node(blob, &soff);
    }
    emit_end_node(blob, &soff);
    emit_token(blob, &soff, FDT_END);
    while ((soff & 3u) != 0) {
        blob[soff++] = 0;
    }

    // ---- Lay out blob: header(40) | rsvmap(16) | struct | strings | tail. ----
    // Move what we just wrote (currently at offset 0) to its real location.
    u32 hdr_size = 40;
    u32 rsvmap_off = hdr_size;
    u32 rsvmap_size = 16; // single (0,0) terminator
    u32 struct_off = rsvmap_off + rsvmap_size; // 56, already 8-aligned
    u32 struct_len = soff;
    u32 strings_off = struct_off + struct_len;
    u32 strings_len = slen;
    u32 totalsize = strings_off + strings_len;

    static u8 final_blob[2048] __attribute__((aligned(8)));
    memset(final_blob, 0, sizeof(final_blob));

    // Header.
    put_be32(final_blob + 0, FDT_MAGIC);
    put_be32(final_blob + 4, totalsize);
    put_be32(final_blob + 8, struct_off);
    put_be32(final_blob + 12, strings_off);
    put_be32(final_blob + 16, rsvmap_off);
    put_be32(final_blob + 20, 17);    // version
    put_be32(final_blob + 24, 16);    // last_comp_version
    put_be32(final_blob + 28, 0);     // boot_cpuid_phys
    put_be32(final_blob + 32, strings_len);
    put_be32(final_blob + 36, struct_len);

    // Rsvmap: just the (0,0) terminator (already zero from memset).

    // Struct block.
    memcpy(final_blob + struct_off, blob, struct_len);

    // Strings.
    memcpy(final_blob + strings_off, strings, strings_len);

    // ---- Open and exercise. ----
    struct Fdt fdt;
    if (Fdt_Open(&fdt, final_blob) != CARA_EOK) {
        return fail("Fdt_Open on hand-built blob failed", 1);
    }

    u32 root = Fdt_Root(&fdt);
    if (root != 0) {
        return fail("root not at offset 0", 2);
    }

    const char *root_name = Fdt_NodeName(&fdt, root);
    if (!root_name || root_name[0] != 0) {
        return fail("root name not empty", 3);
    }

    // Resolve "/" → root.
    u32 r2 = 0;
    if (Fdt_ResolvePath(&fdt, "/", &r2) != CARA_EOK || r2 != root) {
        return fail("ResolvePath / mismatch", 4);
    }

    // Resolve /child@1000 (full path with unit-addr).
    u32 child = 0;
    if (Fdt_ResolvePath(&fdt, "/child@1000", &child) != CARA_EOK) {
        return fail("ResolvePath /child@1000 failed", 5);
    }

    // Resolve /child (without unit-addr) — must also match.
    u32 child2 = 0;
    if (Fdt_ResolvePath(&fdt, "/child", &child2) != CARA_EOK || child2 != child) {
        return fail("ResolvePath /child mismatch", 6);
    }

    if (!Fdt_NodeIsCompatible(&fdt, child, "test,child")) {
        return fail("compatible test,child not matched", 7);
    }
    if (Fdt_NodeIsCompatible(&fdt, child, "no,such")) {
        return fail("spurious compatible match", 8);
    }

    // PropU32
    u32 m32 = 0;
    if (Fdt_PropU32(&fdt, child, "magic", &m32) != CARA_EOK || m32 != 0xCAFEBABEu) {
        return fail("PropU32 wrong", 9);
    }

    // PropU64
    u64 m64 = 0;
    if (Fdt_PropU64(&fdt, child, "magic64", &m64) != CARA_EOK
        || m64 != 0x123456789ABCDEF0ull) {
        return fail("PropU64 wrong", 10);
    }

    // PropStr
    const char *cs = Fdt_PropStr(&fdt, child, "compatible");
    if (!cs || strcmp(cs, "test,child") != 0) {
        return fail("PropStr compatible wrong", 11);
    }

    // PropStrIter on string-list
    u32 cur = 0;
    const char *got = nullptr;
    static const char *expected[] = { "alpha", "beta", "gamma" };
    for (int i = 0; i < 3; i++) {
        int rc = Fdt_PropStrIter(&fdt, child, "string-list", &cur, &got);
        if (rc != CARA_EOK) {
            return fail("PropStrIter early end", 12);
        }
        if (strcmp(got, expected[i]) != 0) {
            return fail("PropStrIter wrong string", 13);
        }
    }
    if (Fdt_PropStrIter(&fdt, child, "string-list", &cur, &got) != CARA_ENOTFOUND) {
        return fail("PropStrIter past end did not return ENOTFOUND", 14);
    }

    // PropReg with parent #address-cells=1 #size-cells=1.
    u64 base = 0, size = 0;
    if (Fdt_PropReg(&fdt, child, 0, &base, &size) != CARA_EOK) {
        return fail("PropReg failed", 15);
    }
    if (base != 0x1000 || size != 0x100) {
        return fail("PropReg returned wrong base/size", 16);
    }

    // PropRaw on a zero-length boolean-style property.
    const void *bytes = nullptr;
    u32 len = 999;
    if (Fdt_PropRaw(&fdt, child, "present", &bytes, &len) != CARA_EOK) {
        return fail("PropRaw on zero-length missed", 17);
    }
    if (len != 0) {
        return fail("PropRaw zero-length reported wrong length", 18);
    }

    // ChildIter from root yields exactly one child.
    u32 cursor = 0, c = 0;
    if (Fdt_ChildIter(&fdt, root, &cursor, &c) != CARA_EOK || c != child) {
        return fail("ChildIter first call wrong", 19);
    }
    if (Fdt_ChildIter(&fdt, root, &cursor, &c) != CARA_ENOTFOUND) {
        return fail("ChildIter past end did not return ENOTFOUND", 20);
    }

    // FindByCompatible matches test,child.
    u32 fc = 0;
    if (Fdt_FindByCompatible(&fdt, "test,child", &fc) != CARA_EOK || fc != child) {
        return fail("FindByCompatible test,child wrong", 21);
    }
    // ... and ENOTFOUND on next call.
    if (Fdt_FindByCompatible(&fdt, "test,child", &fc) != CARA_ENOTFOUND) {
        return fail("FindByCompatible past end did not ENOTFOUND", 22);
    }

    // Memory reservation iterator on a (0,0)-only blob: ENOTFOUND immediately.
    u32 rcur = 0;
    u64 rb = 0, rs = 0;
    if (Fdt_RsvIter(&fdt, &rcur, &rb, &rs) != CARA_ENOTFOUND) {
        return fail("RsvIter on empty rsvmap did not ENOTFOUND", 23);
    }

    puts("fdt walk smoke ok");
    return 0;
}
