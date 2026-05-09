// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ canonical integer and pointer types (Release 2 /
// Kickstart 2.04 — the 3rd Edition RKMs in amigaos_kb_markdown/).
//
// Width choices match V36+ exec/types.i verbatim, with the explicit
// CaraOS deviation that BPTR is pointer-sized rather than a 32-bit
// BCPL longword (CaraOS does not run BCPL — see DRIFT_2026-05.md M3
// and PRINCIPLES.md §3.1). Code that round-trips BPTR through LONG
// was implicitly assuming 32-bit pointers and is a portability bug
// to fix in the source; the BPTR name itself is preserved so the
// V36+ idiom OpenLibrary/Open/Lock/Read/Write etc. compiles.
//
// AmigaOS-namespace source (#include <exec/types.h>) sees these.
// Brand-namespace kernel source uses <cara/types.h> and the
// u8/u16/u32/u64 / i8/i16/i32/i64 aliases. The canonical AmigaOS
// names alias onto the same fixed-width integer types.

#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H

#include <cara/types.h>

typedef int8_t BYTE;
typedef uint8_t UBYTE;
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int32_t LONG; // V36+ 32-bit, NOT C `long` on RV64
typedef uint32_t ULONG;

typedef void *APTR;
typedef void *BPTR; // opaque pointer-sized; non-BCPL on CaraOS
typedef LONG BSTR;  // BCPL string descriptor — opaque to CaraOS
typedef char *STRPTR;
typedef ULONG CPTR; // legacy 32-bit absolute pointer
typedef WORD RPTR;  // relative pointer

// AROS-style pointer-sized integer types. V36+ predates 64-bit so its
// utility.library TagItem ti_Data was ULONG (32-bit). CaraOS follows
// AROS in widening that field to IPTR so tag values can carry SASOS
// pointers — see <utility/tagitem.h>. Source that read ti_Data as
// ULONG truncates on RV64 and is a portability bug.
typedef uintptr_t IPTR;
typedef intptr_t SIPTR;

typedef WORD BOOL;
#ifndef TRUE
#define TRUE ((BOOL)1)
#endif
#ifndef FALSE
#define FALSE ((BOOL)0)
#endif

#ifndef NULL
#define NULL nullptr
#endif

#endif // EXEC_TYPES_H
