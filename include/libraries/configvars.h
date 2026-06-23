// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <libraries/configvars.h> — the expansion.library AutoConfig
// ABI: struct ConfigDev (one per board), its embedded ExpansionRom (the
// board's ROM id), and ExpansionControl. Verbatim V36+ names/fields so a
// V36 program that FindConfigDev's a board compiles unchanged
// (docs/PRINCIPLES §3.1). On CaraOS the ConfigDev list is synthesised from
// the FDT/PCI boot inventory, not read off a Zorro bus (docs/EXPANSION.md).

#ifndef LIBRARIES_CONFIGVARS_H
#define LIBRARIES_CONFIGVARS_H

#include <exec/libraries.h>
#include <exec/nodes.h>
#include <exec/types.h>

// A board's AutoConfig ROM id (synthesised from PCI vendor/product here).
struct ExpansionRom {
    UBYTE er_Type;
    UBYTE er_Product;
    UBYTE er_Flags;
    UBYTE er_Reserved03;
    UWORD er_Manufacturer;
    ULONG er_SerialNumber;
    UWORD er_InitDiagVec;
    UBYTE er_Reserved0c;
    UBYTE er_Reserved0d;
    UBYTE er_Reserved0e;
    UBYTE er_Reserved0f;
};

// er_Type top bits — board class. ERT_NEWBOARD marks a V36-style board.
#define ERT_TYPEMASK 0xC0
#define ERT_NEWBOARD 0xC0
#define ERT_ZORROII 0xC0
#define ERT_ZORROIII 0x80
// er_Type / er_Flags low bits.
#define ERTB_MEMLIST 5
#define ERTF_MEMLIST (1 << 5)
#define ERFB_MEMLIST 5
#define ERFF_MEMLIST (1 << 5)

// The post-config Zorro control registers — ABI-only on CaraOS.
struct ExpansionControl {
    UBYTE ec_Interrupt;
    UBYTE ec_Z3_HighBase;
    UBYTE ec_BaseAddress;
    UBYTE ec_Shutup;
    UBYTE ec_Reserved04;
    UBYTE ec_Reserved05;
    UBYTE ec_Reserved06;
    UBYTE ec_Reserved07;
};

// One configured expansion board.
struct ConfigDev {
    struct Node cd_Node;
    UBYTE cd_Flags; // CDF_*
    UBYTE cd_Pad;
    struct ExpansionRom cd_Rom; // board ROM id (by value)
    APTR cd_BoardAddr;          // where the board was placed in memory
    ULONG cd_BoardSize;         // bytes reserved for the board
    UWORD cd_SlotAddr;
    UWORD cd_SlotSize;
    APTR cd_Driver;              // driver private
    struct ConfigDev *cd_NextCD; // linked list of all boards
    ULONG cd_Unused[4];
};

// cd_Flags.
#define CDB_CONFIGME 1
#define CDF_CONFIGME (1 << 1)
#define CDB_BADMEMORY 2
#define CDF_BADMEMORY (1 << 2)
#define CDB_PROCESSED 3
#define CDF_PROCESSED (1 << 3)

// FindConfigDev wildcard for the manufacturer / product args.
#define FINDCONFIGDEV_ANY (-1)

// expansion.library's base.
struct ExpansionBase {
    struct Library LibNode;
};

#endif // LIBRARIES_CONFIGVARS_H
