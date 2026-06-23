// SPDX-License-Identifier: BSD-2-Clause
//
// diskfont.library OpenDiskFont (L12.2, docs/DISKFONT.md). diskfont loads
// a font from disk into a graphics struct TextFont; graphics.library/Dath
// renders it from the strike (the generic strike path, L12.1). The on-disk
// font is a CaraOS-native Cara font file (§2.2) — a flat serialisation of
// the classic bitmap strike, parsed into one shared-heap allocation
// (TextFont + strike + charLoc [+ charSpace/charKern]) so CloseFont is a
// single free. No 68k hunk / LoadSeg.
//
// OpenDiskFont derives FONTS:<name>/<ysize> from the TextAttr and reads it
// via the dos bridge (Croi_Dos_Open/Read), so it needs a Process — the
// kernel test drives Croi_Diskfont_ParseFont directly instead.

#include <cara/alloc.h>
#include <cara/carafs.h>
#include <cara/carafs_bind.h>
#include <cara/diskfont_lib.h>
#include <cara/dos_lib.h>
#include <cara/shared.h>
#include <cara/types.h>
#include <dos/dos.h>
#include <exec/types.h>
#include <graphics/text.h>
#include <libraries/diskfont.h>

// ---- self-contained byte helpers ------------------------------------

static u16 df_rd16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 df_rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void df_bcopy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    for (u32 i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static void df_bzero(void *dst, u32 n)
{
    u8 *d = (u8 *)dst;
    for (u32 i = 0; i < n; i++) {
        d[i] = 0;
    }
}

static u32 df_align8(u32 n)
{
    return (n + 7u) & ~7u;
}

static u32 df_strlen(const char *s)
{
    u32 n = 0;
    if (s) {
        while (s[n]) {
            n++;
        }
    }
    return n;
}

// ---- the codec ------------------------------------------------------

struct TextFont *Croi_Diskfont_ParseFont(const u8 *blob, u32 len)
{
    if (!blob || len < CARA_CFNT_HEADER) {
        return nullptr;
    }
    if (!(blob[0] == 'C' && blob[1] == 'F' && blob[2] == 'N' && blob[3] == 'T')) {
        return nullptr;
    }
    u16 version = df_rd16(blob + 4);
    if (version != CARA_CFNT_VERSION) {
        return nullptr;
    }
    u16 flags = df_rd16(blob + 6);
    u16 ySize = df_rd16(blob + 8);
    u16 xSize = df_rd16(blob + 10);
    u16 baseline = df_rd16(blob + 12);
    u16 boldSmear = df_rd16(blob + 14);
    u16 modulo = df_rd16(blob + 16);
    u16 style = df_rd16(blob + 18);
    u8 loChar = blob[20];
    u8 hiChar = blob[21];
    if (hiChar < loChar || ySize == 0 || modulo == 0) {
        return nullptr;
    }
    bool prop = (flags & CARA_CFNT_F_PROPORTIONAL) != 0;
    u32 nGlyphs = (u32)hiChar - loChar + 2; // +1 default glyph slot

    u32 strike_bytes = (u32)modulo * ySize;
    u32 charloc_bytes = nGlyphs * 4u;
    u32 space_bytes = prop ? nGlyphs * 2u : 0u;
    u32 kern_bytes = prop ? nGlyphs * 2u : 0u;
    u32 need = CARA_CFNT_HEADER + strike_bytes + charloc_bytes + space_bytes + kern_bytes;
    if (len < need) {
        return nullptr;
    }

    u32 tf_off = df_align8((u32)sizeof(struct TextFont));
    u32 total = tf_off + strike_bytes + charloc_bytes + space_bytes + kern_bytes;
    u8 *base = (u8 *)Croi_AllocShared(total);
    if (!base) {
        return nullptr;
    }
    df_bzero(base, total);

    struct TextFont *tf = (struct TextFont *)base;
    u8 *strike = base + tf_off;
    u32 *charloc = (u32 *)(strike + strike_bytes);

    u32 off = CARA_CFNT_HEADER;
    df_bcopy(strike, blob + off, strike_bytes);
    off += strike_bytes;
    for (u32 g = 0; g < nGlyphs; g++) {
        charloc[g] = df_rd32(blob + off + g * 4u);
    }
    off += charloc_bytes;

    tf->tf_YSize = ySize;
    tf->tf_XSize = xSize;
    tf->tf_Baseline = baseline;
    tf->tf_BoldSmear = boldSmear;
    tf->tf_Style = (UBYTE)style;
    tf->tf_Flags = (UBYTE)(FPF_DISKFONT | (prop ? FPF_PROPORTIONAL : 0));
    tf->tf_LoChar = loChar;
    tf->tf_HiChar = hiChar;
    tf->tf_Accessors = 1;
    tf->tf_CharData = strike;
    tf->tf_Modulo = modulo;
    tf->tf_CharLoc = charloc;

    if (prop) {
        u16 *space = (u16 *)((u8 *)charloc + charloc_bytes);
        i16 *kern = (i16 *)((u8 *)space + space_bytes);
        for (u32 g = 0; g < nGlyphs; g++) {
            space[g] = df_rd16(blob + off + g * 2u);
        }
        off += space_bytes;
        for (u32 g = 0; g < nGlyphs; g++) {
            kern[g] = (i16)df_rd16(blob + off + g * 2u);
        }
        tf->tf_CharSpace = space;
        tf->tf_CharKern = kern;
    }
    return tf;
}

// Build ":Fonts/<name without .font>/<ysize>" into out (cap bytes) — a
// root-relative path to the volume's Fonts directory (FONTS: has no dos
// assign in v0; the leading ':' resets resolution to the volume root).
static bool df_build_path(STRPTR name, u16 ysize, char *out, u32 cap)
{
    static const char pre[] = ":Fonts/";
    u32 p = 0;
    for (u32 i = 0; pre[i]; i++) {
        if (p + 1 >= cap) {
            return false;
        }
        out[p++] = pre[i];
    }
    u32 nlen = df_strlen(name);
    // Strip a trailing ".font" so "topaz.font" → "topaz".
    if (nlen >= 5 && name[nlen - 5] == '.' && name[nlen - 4] == 'f' && name[nlen - 3] == 'o' &&
        name[nlen - 2] == 'n' && name[nlen - 1] == 't') {
        nlen -= 5;
    }
    if (nlen == 0) {
        return false;
    }
    for (u32 i = 0; i < nlen; i++) {
        if (p + 1 >= cap) {
            return false;
        }
        out[p++] = name[i];
    }
    if (p + 1 >= cap) {
        return false;
    }
    out[p++] = '/';
    // Decimal ysize.
    char tmp[8];
    u32 t = 0, v = ysize;
    if (v == 0) {
        tmp[t++] = '0';
    }
    while (v) {
        tmp[t++] = (char)('0' + v % 10u);
        v /= 10u;
    }
    while (t) {
        if (p + 1 >= cap) {
            return false;
        }
        out[p++] = tmp[--t];
    }
    out[p] = 0;
    return true;
}

struct TextFont *Croi_Diskfont_OpenDiskFont_Impl(struct TextAttr *textAttr)
{
    if (!textAttr || !textAttr->ta_Name) {
        return nullptr;
    }
    char path[64];
    if (!df_build_path(textAttr->ta_Name, textAttr->ta_YSize, path, sizeof path)) {
        return nullptr;
    }
    BPTR fh = Croi_Dos_Open_Impl((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        return nullptr;
    }
    static u8 buf[CARA_DISKFONT_MAX];
    LONG n = Croi_Dos_Read_Impl(fh, buf, (LONG)sizeof buf);
    Croi_Dos_Close_Impl(fh);
    if (n <= 0) {
        return nullptr;
    }
    return Croi_Diskfont_ParseFont(buf, (u32)n);
}

// ---- font enumeration (L12.3) ---------------------------------------
// Enumeration walks CaraFS directly (g_carafs) rather than dos, so it
// needs no Process: the fonts live at root/Fonts/<family>/<ysize>.

#define DF_NO_DIR (~0ull)

// The volume's Fonts directory cnode, or DF_NO_DIR if absent.
static u64 df_fonts_dir(void)
{
    if (!g_carafs_mounted) {
        return DF_NO_DIR;
    }
    u64 c;
    u16 t;
    if (Carafs_DirLookup(&g_carafs, g_carafs.sb.root_cnode, "Fonts", 5, &c, &t) != CARA_EOK) {
        return DF_NO_DIR;
    }
    return (t == CARAFS_T_DIR) ? c : DF_NO_DIR;
}

// Parse a decimal directory-entry name (e.g. "8") into *out. false if it
// is empty or has a non-digit (so non-size files are skipped).
static bool df_name_uint(const u8 *name, u32 len, u32 *out)
{
    if (len == 0 || len > 5) {
        return false;
    }
    u32 v = 0;
    for (u32 i = 0; i < len; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        v = v * 10u + (u32)(name[i] - '0');
    }
    *out = v;
    return true;
}

static u32 df_cstrlen(const char *s)
{
    u32 n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

// AvailFonts: pack an AvailFontsHeader + AvailFonts records (ta_Name into
// the buffer) for the ROM face (AFF_MEMORY) + each disk font under :Fonts
// (AFF_DISK). Returns 0 if it fit, else the extra bytes needed.
LONG Croi_Diskfont_AvailFonts_Impl(STRPTR buffer, ULONG bufBytes, ULONG flags)
{
    const u32 afrec = (u32)sizeof(struct AvailFonts);
    const u32 hdr = 8u; // align the records to 8 (AvailFonts holds a pointer)
    static const char rom_name[] = "topaz.font";
    u32 rom_len = (u32)sizeof rom_name; // includes NUL

    u64 fdir = df_fonts_dir();
    bool want_mem = (flags & AFF_MEMORY) != 0;
    bool want_disk = (flags & AFF_DISK) != 0 && fdir != DF_NO_DIR;

    // Pass 1: count entries + string bytes.
    u32 nent = 0, strbytes = 0;
    if (want_mem) {
        nent++;
        strbytes += rom_len;
    }
    if (want_disk) {
        struct CarafsDirCursor fc = { 0 };
        struct CarafsDirEntry fe;
        while (Carafs_DirNext(&g_carafs, fdir, &fc, &fe) == CARA_EOK) {
            if (fe.type != CARAFS_T_DIR) {
                continue;
            }
            struct CarafsDirCursor sc = { 0 };
            struct CarafsDirEntry se;
            while (Carafs_DirNext(&g_carafs, fe.cnode, &sc, &se) == CARA_EOK) {
                u32 ys;
                if (!df_name_uint(se.name, se.name_len, &ys)) {
                    continue;
                }
                nent++;
                strbytes += fe.name_len + 5u + 1u; // "<family>.font\0"
            }
        }
    }

    u32 needed = hdr + nent * afrec + strbytes;
    if (!buffer || needed > bufBytes) {
        return (LONG)(needed > bufBytes ? needed - bufBytes : 0);
    }

    // Pass 2: fill.
    struct AvailFontsHeader *afh = (struct AvailFontsHeader *)buffer;
    afh->afh_NumEntries = (UWORD)nent;
    struct AvailFonts *rec = (struct AvailFonts *)(buffer + hdr);
    char *sp = (char *)buffer + hdr + nent * afrec;
    u32 e = 0;

    if (want_mem) {
        rec[e].af_Type = AFF_MEMORY;
        for (u32 i = 0; i < rom_len; i++) {
            sp[i] = rom_name[i];
        }
        rec[e].af_Attr.ta_Name = (STRPTR)sp;
        rec[e].af_Attr.ta_YSize = 8;
        rec[e].af_Attr.ta_Style = 0;
        rec[e].af_Attr.ta_Flags = FPF_ROMFONT;
        sp += rom_len;
        e++;
    }
    if (want_disk) {
        struct CarafsDirCursor fc = { 0 };
        struct CarafsDirEntry fe;
        while (Carafs_DirNext(&g_carafs, fdir, &fc, &fe) == CARA_EOK) {
            if (fe.type != CARAFS_T_DIR) {
                continue;
            }
            struct CarafsDirCursor sc = { 0 };
            struct CarafsDirEntry se;
            while (Carafs_DirNext(&g_carafs, fe.cnode, &sc, &se) == CARA_EOK) {
                u32 ys;
                if (!df_name_uint(se.name, se.name_len, &ys)) {
                    continue;
                }
                rec[e].af_Type = AFF_DISK | AFF_BITMAP;
                char *nm = sp;
                for (u32 i = 0; i < fe.name_len; i++) {
                    *sp++ = (char)fe.name[i];
                }
                *sp++ = '.';
                *sp++ = 'f';
                *sp++ = 'o';
                *sp++ = 'n';
                *sp++ = 't';
                *sp++ = 0;
                rec[e].af_Attr.ta_Name = (STRPTR)nm;
                rec[e].af_Attr.ta_YSize = (UWORD)ys;
                rec[e].af_Attr.ta_Style = 0;
                rec[e].af_Attr.ta_Flags = FPF_DISKFONT;
                e++;
            }
        }
    }
    return 0;
}

// NewFontContents: build a font's FontContents index from its size files
// under :Fonts/<name>. fontsLock is ignored (v0 uses the volume root).
struct FontContentsHeader *Croi_Diskfont_NewFontContents_Impl(BPTR fontsLock, STRPTR name)
{
    (void)fontsLock;
    if (!name) {
        return nullptr;
    }
    u64 fdir = df_fonts_dir();
    if (fdir == DF_NO_DIR) {
        return nullptr;
    }
    // Strip a trailing ".font".
    u32 nlen = df_cstrlen(name);
    if (nlen >= 5 && name[nlen - 5] == '.' && name[nlen - 4] == 'f' && name[nlen - 3] == 'o' &&
        name[nlen - 2] == 'n' && name[nlen - 1] == 't') {
        nlen -= 5;
    }
    if (nlen == 0 || nlen > CARAFS_NAME_MAX) {
        return nullptr;
    }
    u64 famc;
    u16 t;
    if (Carafs_DirLookup(&g_carafs, fdir, name, nlen, &famc, &t) != CARA_EOK || t != CARAFS_T_DIR) {
        return nullptr;
    }
    // Count size files.
    u32 nsizes = 0;
    {
        struct CarafsDirCursor sc = { 0 };
        struct CarafsDirEntry se;
        while (Carafs_DirNext(&g_carafs, famc, &sc, &se) == CARA_EOK) {
            u32 ys;
            if (df_name_uint(se.name, se.name_len, &ys)) {
                nsizes++;
            }
        }
    }
    if (nsizes == 0) {
        return nullptr;
    }
    u32 total = (u32)sizeof(struct FontContentsHeader) + nsizes * (u32)sizeof(struct FontContents);
    struct FontContentsHeader *fch = (struct FontContentsHeader *)Croi_AllocShared(total);
    if (!fch) {
        return nullptr;
    }
    fch->fch_FileID = FCH_ID;
    fch->fch_NumEntries = (UWORD)nsizes;
    struct FontContents *fc = (struct FontContents *)(fch + 1);
    u32 idx = 0;
    struct CarafsDirCursor sc = { 0 };
    struct CarafsDirEntry se;
    while (idx < nsizes && Carafs_DirNext(&g_carafs, famc, &sc, &se) == CARA_EOK) {
        u32 ys;
        if (!df_name_uint(se.name, se.name_len, &ys)) {
            continue;
        }
        // fc_FileName = "<name>/<size>".
        u32 p = 0;
        for (u32 i = 0; i < nlen && p < MAXFONTPATH - 1; i++) {
            fc[idx].fc_FileName[p++] = name[i];
        }
        if (p < MAXFONTPATH - 1) {
            fc[idx].fc_FileName[p++] = '/';
        }
        for (u32 i = 0; i < se.name_len && p < MAXFONTPATH - 1; i++) {
            fc[idx].fc_FileName[p++] = (char)se.name[i];
        }
        fc[idx].fc_FileName[p] = 0;
        fc[idx].fc_YSize = (UWORD)ys;
        fc[idx].fc_Style = 0;
        fc[idx].fc_Flags = FPF_DISKFONT;
        idx++;
    }
    fch->fch_NumEntries = (UWORD)idx;
    return fch;
}

void Croi_Diskfont_DisposeFontContents_Impl(struct FontContentsHeader *fch)
{
    if (fch) {
        Croi_Free(fch);
    }
}
