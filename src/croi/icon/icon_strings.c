// SPDX-License-Identifier: BSD-2-Clause
//
// icon.library tool-type / revision helpers (L11.3, docs/ICON.md) —
// `local`-flavour LVOs. Pure string operations on the caller's SASOS
// buffers, run in-process from the shared RX page (.lib_text.icon at VA
// 0x4000_0000) and JALR'd directly from U-mode with no trampoline. The
// generated stub appends IconBase as the trailing arg, which we ignore.
//
// RX-page discipline (same as src/croi/exec_lib/list_ops.c + the BOOPSI
// code): self-contained — no kernel symbols, no globals, no out-of-section
// calls (helpers are always-inlined), and NO string literals (those land
// in .rodata ~1 GiB away → a PC-relative reloc that overflows). Every
// constant character is an `int` char literal; output is emitted byte by
// byte. -fno-jump-tables is set on this TU (see CMakeLists.txt).

#include <exec/types.h>

struct IconBase;

#define LIBTEXT __attribute__((section(".lib_text.icon"), used))
#define ICON_INLINE static inline __attribute__((always_inline))

ICON_INLINE ULONG icon_slen(STRPTR s)
{
    ULONG n = 0;
    if (s) {
        while (s[n]) {
            n++;
        }
    }
    return n;
}

// FindToolType: search the NULL-terminated toolTypeArray for an entry
// whose key equals `typeName` (the part before '='), and return a pointer
// to its value — the bytes after '=', or the empty string at the entry's
// NUL when it has no '='. nullptr when not found. Key match is exact.
LIBTEXT STRPTR Croi_Icon_FindToolType(STRPTR *toolTypeArray, STRPTR typeName, struct IconBase *base)
{
    (void)base;
    if (!toolTypeArray || !typeName) {
        return nullptr;
    }
    ULONG n = icon_slen(typeName);
    for (ULONG i = 0; toolTypeArray[i]; i++) {
        STRPTR e = toolTypeArray[i];
        ULONG j = 0;
        while (j < n && e[j] && e[j] == typeName[j]) {
            j++;
        }
        if (j == n && (e[j] == 0 || e[j] == '=')) {
            return (e[j] == '=') ? &e[j + 1] : &e[j];
        }
    }
    return nullptr;
}

// MatchToolValue: TRUE when `value` equals one of the '|'-separated
// alternatives in typeString (exact, case-sensitive). Typical use:
// MatchToolValue(FindToolType(tt, "FLAGS"), "on").
LIBTEXT BOOL Croi_Icon_MatchToolValue(STRPTR typeString, STRPTR value, struct IconBase *base)
{
    (void)base;
    if (!typeString || !value) {
        return FALSE;
    }
    ULONG i = 0;
    for (;;) {
        ULONG k = 0;
        while (value[k] && typeString[i + k] == value[k]) {
            k++;
        }
        if (value[k] == 0 && (typeString[i + k] == 0 || typeString[i + k] == '|')) {
            return TRUE;
        }
        // Skip to the next alternative.
        while (typeString[i] && typeString[i] != '|') {
            i++;
        }
        if (typeString[i] == 0) {
            return FALSE;
        }
        i++; // past '|'
    }
}

// BumpRevision: write a "copy of" name derived from oldname into newname
// (which must hold at least 30 bytes) and return newname. The first copy
// is "copy_of_X", then "copy_2_of_X", "copy_3_of_X", … — an existing
// "copy_of_"/"copy_N_of_" prefix is recognised and its number bumped.
// Truncated to fit the buffer.
#define ICON_BUMP_MAX 30u

ICON_INLINE ULONG icon_put_ch(STRPTR out, ULONG pos, char c)
{
    if (pos < ICON_BUMP_MAX - 1u) {
        out[pos++] = c;
    }
    return pos;
}

LIBTEXT STRPTR Croi_Icon_BumpRevision(STRPTR newname, STRPTR oldname, struct IconBase *base)
{
    (void)base;
    if (!newname) {
        return nullptr;
    }
    char empty = 0;
    STRPTR ob = oldname ? oldname : &empty;

    // Recognise a "copy_of_" (rev 1) or "copy_<n>_of_" (rev n) prefix.
    ULONG rev = 0, bidx = 0;
    if (ob[0] == 'c' && ob[1] == 'o' && ob[2] == 'p' && ob[3] == 'y' && ob[4] == '_') {
        ULONG i = 5, n = 0;
        bool dig = false;
        while (ob[i] >= '0' && ob[i] <= '9') {
            n = n * 10u + (ULONG)(ob[i] - '0');
            i++;
            dig = true;
        }
        if (dig) {
            if (ob[i] == '_' && ob[i + 1] == 'o' && ob[i + 2] == 'f' && ob[i + 3] == '_') {
                rev = n;
                bidx = i + 4;
            }
        } else {
            if (ob[i] == 'o' && ob[i + 1] == 'f' && ob[i + 2] == '_') {
                rev = 1;
                bidx = i + 3;
            }
        }
    }
    ULONG new_rev = rev + 1;

    ULONG p = 0;
    p = icon_put_ch(newname, p, 'c');
    p = icon_put_ch(newname, p, 'o');
    p = icon_put_ch(newname, p, 'p');
    p = icon_put_ch(newname, p, 'y');
    p = icon_put_ch(newname, p, '_');
    if (new_rev > 1) {
        char tmp[12];
        ULONG t = 0, v = new_rev;
        while (v) {
            tmp[t++] = (char)('0' + v % 10u);
            v /= 10u;
        }
        while (t) {
            p = icon_put_ch(newname, p, tmp[--t]);
        }
        p = icon_put_ch(newname, p, '_');
    }
    p = icon_put_ch(newname, p, 'o');
    p = icon_put_ch(newname, p, 'f');
    p = icon_put_ch(newname, p, '_');
    for (ULONG i = 0; ob[bidx + i]; i++) {
        p = icon_put_ch(newname, p, ob[bidx + i]);
    }
    newname[p] = 0;
    return newname;
}
