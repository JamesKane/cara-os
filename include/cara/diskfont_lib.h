// SPDX-License-Identifier: BSD-2-Clause
//
// Kernel-side impl prototypes for the V36+ diskfont.library LVOs Croi
// serves (docs/DISKFONT.md). diskfont is `syscall` flavour: each
// implemented LVO reaches src/croi/syscall/syscall.c via its trampoline
// (Cara_Trampoline_Diskfont_* in src/croi/diskfont/trampolines.S), routed
// to the Croi_Diskfont_*_Impl below. OpenDiskFont reads a Cara font file
// (§2.2) off FONTS: via the dos bridge and parses it into a graphics
// struct TextFont (FPF_DISKFONT) in the SASOS shared heap; the Dath
// rasteriser (L12.1) renders it from the strike. graphics.library CloseFont
// frees a disk font.

#ifndef CARA_DISKFONT_LIB_H
#define CARA_DISKFONT_LIB_H

#include <cara/types.h>
#include <exec/types.h>
#include <graphics/text.h>
#include <libraries/diskfont.h>

struct Library;

// ---- Cara font file format (§2.2) -----------------------------------
// Little-endian. Header (22 bytes): magic 'C','F','N','T' | u16 version |
// u16 flags | u16 ySize | u16 xSize | u16 baseline | u16 boldSmear |
// u16 modulo | u16 style | u8 loChar | u8 hiChar. Then strike[modulo*ySize]
// | u32 charLoc[nGlyphs] | (if proportional) u16 charSpace[nGlyphs] +
// i16 charKern[nGlyphs]. nGlyphs = hiChar - loChar + 2 (the +1 default).
#define CARA_CFNT_VERSION 1
#define CARA_CFNT_F_PROPORTIONAL 0x0001u
#define CARA_CFNT_HEADER 22u
#define CARA_DISKFONT_MAX 8192u // largest font file handled inline

// ---- Reserved-slot library hooks (`local` flavour) ------------------
struct Library *Croi_Diskfont_Open(struct Library *base, struct DiskfontBase *db);
void Croi_Diskfont_Close(struct Library *base, struct DiskfontBase *db);
void Croi_Diskfont_Expunge(struct Library *base, struct DiskfontBase *db);
ULONG Croi_Diskfont_ExtFunc(struct Library *base, struct DiskfontBase *db);

// ---- LVO impls (L12.2) ----------------------------------------------
// OpenDiskFont resolves textAttr → FONTS:<name>/<ysize>, reads + parses
// it into a TextFont (nullptr if absent/malformed).
struct TextFont *Croi_Diskfont_OpenDiskFont_Impl(struct TextAttr *textAttr);

// Parse a Cara font blob into a heap TextFont (nullptr if malformed). The
// dos-free seam OpenDiskFont calls after reading the file; the kernel test
// drives it directly on a hand-built blob.
struct TextFont *Croi_Diskfont_ParseFont(const u8 *blob, u32 len);

// ---- LVO impls (L12.3) ----------------------------------------------
// AvailFonts enumerates the ROM face (AFF_MEMORY) + the disk fonts under
// :Fonts (AFF_DISK), packing an AvailFontsHeader + AvailFonts records (with
// ta_Name strings) into the caller's buffer; returns 0 if it fit, else the
// extra bytes needed. NewFontContents builds a FontContentsHeader from a
// font's size files; DisposeFontContents frees it. Enumeration walks
// CaraFS directly (g_carafs), so it needs no Process.
LONG Croi_Diskfont_AvailFonts_Impl(STRPTR buffer, ULONG bufBytes, ULONG flags);
struct FontContentsHeader *Croi_Diskfont_NewFontContents_Impl(BPTR fontsLock, STRPTR name);
void Croi_Diskfont_DisposeFontContents_Impl(struct FontContentsHeader *fch);

#endif // CARA_DISKFONT_LIB_H
