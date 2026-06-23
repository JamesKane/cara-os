// SPDX-License-Identifier: BSD-2-Clause
//
// icon.library DiskObject read path (L11.1, docs/ICON.md). icon.library
// keeps icon metadata in an object's `cara.icon` extended attribute
// (CARAFS §3.10), not in a sibling `.info` file. GetDiskObject resolves
// the object, reads the xattr, and parses the compact CaraIconBlob
// serialisation into a heap struct DiskObject; FreeDiskObject releases
// the single allocation. The write side (PutDiskObject) and the
// tool-type helpers arrive in L11.2 / L11.3.
//
// The blob is a CaraOS-native format (we own `cara.*`), versioned:
//   u16 magic(0xCA10) | u16 version(1) | u8 type | u8 flags
//   | i32 currentX | i32 currentY | u32 stackSize
//   | cstr defaultTool | cstr toolWindow            (cstr = u16 len + bytes)
//   | u16 n_toolTypes | cstr toolType[n_toolTypes]
//   | (if type==WBDRAWER) i16 le,te,wi,he | i32 ddCurrentX | i32 ddCurrentY
// Imagery is intentionally absent (do_Gadget render fields stay null);
// the file-manager draws a default glyph per do_Type.

#include <cara/alloc.h>
#include <cara/carafs.h>
#include <cara/carafs_bind.h>
#include <cara/dos_lib.h>
#include <cara/icon_lib.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/types.h>
#include <workbench/workbench.h>

#define ICON_BLOB_MAGIC 0xCA10u
#define ICON_BLOB_VERSION 1u
#define ICON_MAX_TOOLTYPES 64

// ---- self-contained byte helpers (no libc dependency) ---------------

static u32 icon_strlen(const char *s)
{
    u32 n = 0;
    if (s) {
        while (s[n]) {
            n++;
        }
    }
    return n;
}

static void icon_bcopy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    for (u32 i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void icon_bzero(void *dst, u32 n)
{
    u8 *d = (u8 *)dst;
    for (u32 i = 0; i < n; i++) {
        d[i] = 0;
    }
}

static u32 icon_align8(u32 n)
{
    return (n + 7u) & ~7u;
}

// ---- blob writer ----------------------------------------------------

struct wcur {
    u8 *p;
    u32 cap;
    u32 off;
    bool ok;
};

static void w_u8(struct wcur *w, u8 v)
{
    if (w->off + 1 > w->cap) {
        w->ok = false;
        return;
    }
    w->p[w->off++] = v;
}

static void w_u16(struct wcur *w, u16 v)
{
    if (w->off + 2 > w->cap) {
        w->ok = false;
        return;
    }
    w->p[w->off++] = (u8)(v & 0xFFu);
    w->p[w->off++] = (u8)((v >> 8) & 0xFFu);
}

static void w_u32(struct wcur *w, u32 v)
{
    if (w->off + 4 > w->cap) {
        w->ok = false;
        return;
    }
    w->p[w->off++] = (u8)(v & 0xFFu);
    w->p[w->off++] = (u8)((v >> 8) & 0xFFu);
    w->p[w->off++] = (u8)((v >> 16) & 0xFFu);
    w->p[w->off++] = (u8)((v >> 24) & 0xFFu);
}

static void w_str(struct wcur *w, const char *s)
{
    u32 n = icon_strlen(s);
    if (n > 0xFFFFu) {
        w->ok = false;
        return;
    }
    w_u16(w, (u16)n);
    if (w->off + n > w->cap) {
        w->ok = false;
        return;
    }
    icon_bcopy(w->p + w->off, s, n);
    w->off += n;
}

u32 Croi_Icon_BlobBuild(const struct DiskObject *d, u8 *buf, u32 buflen)
{
    if (!d || !buf) {
        return 0;
    }
    struct wcur w = { buf, buflen, 0, true };
    w_u16(&w, ICON_BLOB_MAGIC);
    w_u16(&w, ICON_BLOB_VERSION);
    w_u8(&w, d->do_Type);
    w_u8(&w, 0); // flags
    w_u32(&w, (u32)d->do_CurrentX);
    w_u32(&w, (u32)d->do_CurrentY);
    w_u32(&w, (u32)d->do_StackSize);
    w_str(&w, d->do_DefaultTool);
    w_str(&w, d->do_ToolWindow);

    u32 ntt = 0;
    if (d->do_ToolTypes) {
        while (ntt < ICON_MAX_TOOLTYPES && d->do_ToolTypes[ntt]) {
            ntt++;
        }
    }
    w_u16(&w, (u16)ntt);
    for (u32 i = 0; i < ntt; i++) {
        w_str(&w, d->do_ToolTypes[i]);
    }

    if (d->do_Type == WBDRAWER) {
        const struct DrawerData *dd = d->do_DrawerData;
        i16 le = 0, te = 0, wi = 0, he = 0;
        i32 ddx = 0, ddy = 0;
        if (dd) {
            le = dd->dd_NewWindow.LeftEdge;
            te = dd->dd_NewWindow.TopEdge;
            wi = dd->dd_NewWindow.Width;
            he = dd->dd_NewWindow.Height;
            ddx = dd->dd_CurrentX;
            ddy = dd->dd_CurrentY;
        }
        w_u16(&w, (u16)le);
        w_u16(&w, (u16)te);
        w_u16(&w, (u16)wi);
        w_u16(&w, (u16)he);
        w_u32(&w, (u32)ddx);
        w_u32(&w, (u32)ddy);
    }
    return w.ok ? w.off : 0;
}

// ---- blob reader ----------------------------------------------------

struct rcur {
    const u8 *p;
    u32 len;
    u32 off;
    bool ok;
};

static u8 r_u8(struct rcur *r)
{
    if (r->off + 1 > r->len) {
        r->ok = false;
        return 0;
    }
    return r->p[r->off++];
}

static u16 r_u16(struct rcur *r)
{
    if (r->off + 2 > r->len) {
        r->ok = false;
        return 0;
    }
    u16 v = (u16)((u16)r->p[r->off] | ((u16)r->p[r->off + 1] << 8));
    r->off += 2;
    return v;
}

static u32 r_u32(struct rcur *r)
{
    if (r->off + 4 > r->len) {
        r->ok = false;
        return 0;
    }
    u32 v = (u32)r->p[r->off] | ((u32)r->p[r->off + 1] << 8) | ((u32)r->p[r->off + 2] << 16) |
            ((u32)r->p[r->off + 3] << 24);
    r->off += 4;
    return v;
}

// Counted-string read: returns the byte offset of the string within the
// blob and its length, advancing the cursor past it.
static u32 r_str(struct rcur *r, u16 *len_out)
{
    u16 n = r_u16(r);
    if (!r->ok || r->off + n > r->len) {
        r->ok = false;
        *len_out = 0;
        return 0;
    }
    u32 boff = r->off;
    r->off += n;
    *len_out = n;
    return boff;
}

static struct DiskObject *icon_blob_parse(const u8 *blob, u32 len)
{
    struct rcur r = { blob, len, 0, true };
    u16 magic = r_u16(&r);
    u16 version = r_u16(&r);
    if (!r.ok || magic != ICON_BLOB_MAGIC || version != ICON_BLOB_VERSION) {
        return nullptr;
    }
    u8 type = r_u8(&r);
    (void)r_u8(&r); // flags
    i32 cx = (i32)r_u32(&r);
    i32 cy = (i32)r_u32(&r);
    u32 stack = r_u32(&r);

    u16 dt_len;
    u32 dt_off = r_str(&r, &dt_len);
    u16 tw_len;
    u32 tw_off = r_str(&r, &tw_len);
    u16 ntt = r_u16(&r);
    if (!r.ok || ntt > ICON_MAX_TOOLTYPES) {
        return nullptr;
    }
    u32 tt_off[ICON_MAX_TOOLTYPES];
    u16 tt_len[ICON_MAX_TOOLTYPES];
    for (u16 i = 0; i < ntt; i++) {
        tt_off[i] = r_str(&r, &tt_len[i]);
    }

    bool drawer = (type == WBDRAWER);
    i16 le = 0, te = 0, wi = 0, he = 0;
    i32 ddx = 0, ddy = 0;
    if (drawer) {
        le = (i16)r_u16(&r);
        te = (i16)r_u16(&r);
        wi = (i16)r_u16(&r);
        he = (i16)r_u16(&r);
        ddx = (i32)r_u32(&r);
        ddy = (i32)r_u32(&r);
    }
    if (!r.ok) {
        return nullptr;
    }

    // One shared-heap allocation: DiskObject [+ DrawerData] + ToolTypes
    // vector (NULL-terminated) + the string bytes.
    u32 sz = icon_align8((u32)sizeof(struct DiskObject));
    u32 dd_at = 0;
    if (drawer) {
        dd_at = sz;
        sz += icon_align8((u32)sizeof(struct DrawerData));
    }
    u32 vec_at = sz;
    sz += icon_align8((ntt + 1u) * (u32)sizeof(char *));
    u32 str_at = sz;
    u32 str_total = (u32)dt_len + 1u + (tw_len ? (u32)tw_len + 1u : 0u);
    for (u16 i = 0; i < ntt; i++) {
        str_total += (u32)tt_len[i] + 1u;
    }
    sz += str_total;

    u8 *base = (u8 *)Croi_AllocShared(sz);
    if (!base) {
        return nullptr;
    }
    icon_bzero(base, sz);

    struct DiskObject *d = (struct DiskObject *)base;
    d->do_Magic = WB_DISKMAGIC;
    d->do_Version = WB_DISKVERSION;
    d->do_Type = type;
    d->do_CurrentX = cx;
    d->do_CurrentY = cy;
    d->do_StackSize = (LONG)stack;

    char **vec = (char **)(base + vec_at);
    d->do_ToolTypes = vec;

    u8 *sp = base + str_at;
    // defaultTool is always materialised (empty string allowed).
    d->do_DefaultTool = (char *)sp;
    icon_bcopy(sp, blob + dt_off, dt_len);
    sp[dt_len] = 0;
    sp += (u32)dt_len + 1u;
    // toolWindow is null when absent.
    if (tw_len) {
        d->do_ToolWindow = (char *)sp;
        icon_bcopy(sp, blob + tw_off, tw_len);
        sp[tw_len] = 0;
        sp += (u32)tw_len + 1u;
    }
    for (u16 i = 0; i < ntt; i++) {
        vec[i] = (char *)sp;
        icon_bcopy(sp, blob + tt_off[i], tt_len[i]);
        sp[tt_len[i]] = 0;
        sp += (u32)tt_len[i] + 1u;
    }
    vec[ntt] = nullptr;

    if (drawer) {
        struct DrawerData *dd = (struct DrawerData *)(base + dd_at);
        d->do_DrawerData = dd;
        dd->dd_NewWindow.LeftEdge = le;
        dd->dd_NewWindow.TopEdge = te;
        dd->dd_NewWindow.Width = wi;
        dd->dd_NewWindow.Height = he;
        dd->dd_CurrentX = ddx;
        dd->dd_CurrentY = ddy;
    }
    return d;
}

// ---- impls ----------------------------------------------------------

struct DiskObject *Croi_Icon_ReadCnode(struct CarafsMount *m, u64 cnode)
{
    if (!m) {
        return nullptr;
    }
    u8 buf[CARA_ICON_BLOB_MAX];
    usize len = 0;
    if (Carafs_XattrGet(m, cnode, "cara.icon", 9, buf, sizeof buf, &len) != CARA_EOK) {
        return nullptr;
    }
    if (len == 0 || len > sizeof buf) {
        return nullptr;
    }
    return icon_blob_parse(buf, (u32)len);
}

struct DiskObject *Croi_Icon_GetDiskObject_Impl(STRPTR name)
{
    if (!name) {
        return nullptr;
    }
    BPTR lock = Croi_Dos_Lock_Impl(name, SHARED_LOCK);
    if (!lock) {
        return nullptr;
    }
    struct FileLock *fl = (struct FileLock *)BADDR(lock);
    u64 cnode = (u64)(uptr)fl->fl_Key;
    struct DiskObject *d = Croi_Icon_ReadCnode(&g_carafs, cnode);
    Croi_Dos_UnLock_Impl(lock);
    return d;
}

void Croi_Icon_FreeDiskObject_Impl(struct DiskObject *diskobj)
{
    if (diskobj) {
        Croi_Free(diskobj); // the single shared-heap allocation
    }
}
