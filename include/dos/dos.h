// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ dos.library public ABI — the application-facing types
// and constants (3rd Edition RKMs / dos/dos.h). The library functions
// themselves arrive via <proto/dos.h> (generated from
// tools/lvo-gen/dos.conf). See docs/LOGAIC_DOS.md for the design.
//
// BCPL heritage: AmigaDOS passes BCPL pointers (BPTR) and strings
// (BSTR) at the ABI. Classic BPTR is a machine address >> 2. CaraOS
// instead keeps BPTR as a *real* pointer-width value (no >>2 shift) —
// the representation already chosen in <exec/types.h> — so the BADDR /
// MKBADDR conversions are identity casts. A program that calls
// BADDR(lock) gets the object pointer either way; only code that does
// raw numeric BPTR arithmetic (which SASOS already breaks) cares.
// BSTR is a BPTR to a length-prefixed (not NUL-terminated) string,
// converted at the dos boundary (docs/LOGAIC_DOS.md §2.1).

#ifndef DOS_DOS_H
#define DOS_DOS_H

#include <exec/types.h> // BPTR, BSTR

#define BADDR(b) ((void *)(b))
#define MKBADDR(p) ((BPTR)(p))
#define BNULL ((BPTR)0)

// Boolean returns from dos.library are the AmigaDOS convention, not C's.
#define DOSTRUE (-1L)
#define DOSFALSE (0L)

// Open() modes.
#define MODE_OLDFILE 1005   // existing file, R/W, shared lock
#define MODE_NEWFILE 1006   // create/truncate, R/W, exclusive lock
#define MODE_READWRITE 1004 // existing or new, R/W, shared lock

// Lock() access modes.
#define SHARED_LOCK (-2)
#define ACCESS_READ (-2)
#define EXCLUSIVE_LOCK (-1)
#define ACCESS_WRITE (-1)

// Seek() modes.
#define OFFSET_BEGINNING (-1)
#define OFFSET_CURRENT (0)
#define OFFSET_END (1)

// fib_DirEntryType / fib_EntryType signs (>0 directory, <0 file).
#define ST_ROOT 1
#define ST_USERDIR 2
#define ST_SOFTLINK 3
#define ST_LINKDIR 4
#define ST_FILE (-3)
#define ST_LINKFILE (-4)

// A small, common subset of the dos.library error codes (IoErr()
// values). The full set lands as later slices need them.
#define ERROR_NO_FREE_STORE 103
#define ERROR_OBJECT_IN_USE 202
#define ERROR_OBJECT_EXISTS 203
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_DISK_WRITE_PROTECTED 214
#define ERROR_SEEK_ERROR 219
#define ERROR_DISK_FULL 221
#define ERROR_NO_MORE_ENTRIES 232

// V36+ DateStamp — days/minutes/ticks since 1978-01-01 (50 ticks/sec).
struct DateStamp {
    LONG ds_Days;
    LONG ds_Minute;
    LONG ds_Tick;
};

// Examine()/ExNext() result block (ABI-frozen field order).
struct FileInfoBlock {
    LONG fib_DiskKey;
    LONG fib_DirEntryType; // >0 directory, <0 file (ST_* above)
    char fib_FileName[108];
    LONG fib_Protection;
    LONG fib_EntryType;
    LONG fib_Size;
    LONG fib_NumBlocks;
    struct DateStamp fib_Date;
    char fib_Comment[80];
    UWORD fib_OwnerUID;
    UWORD fib_OwnerGID;
    char fib_Reserved[32];
};

#endif // DOS_DOS_H
