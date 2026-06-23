// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ <libraries/commodities.h> — the commodities.library ABI:
// the CxObj broker/object network, the CxMsg routed message, the NewBroker
// init record, and the InputXpression filter. Verbatim V36+ names/fields so
// a V36 commodity program compiles unchanged (docs/PRINCIPLES §3.1). On
// CaraOS L13 ships the object model + IX parsing; the live input dispatch
// (the input.device handler chain) is deferred (docs/COMMODITIES.md).

#ifndef LIBRARIES_COMMODITIES_H
#define LIBRARIES_COMMODITIES_H

#include <devices/inputevent.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <exec/types.h>

// CxObj / CxMsg are opaque — a program only ever holds the pointer.
struct CxObj;
struct CxMsg;

// CxObj types (CreateCxObj's first arg; CxObjType's result).
#define CX_INVALID 0
#define CX_FILTER 1
#define CX_TYPEFILTER 2
#define CX_SEND 3
#define CX_SIGNAL 4
#define CX_TRANSLATE 5
#define CX_BROKER 6
#define CX_DEBUG 7
#define CX_CUSTOM 8
#define CX_ZERO 9

// CxObjError bits (CxObjError's result).
#define COERR_ISNULL (1 << 0)
#define COERR_NULLATTACH (1 << 1)
#define COERR_BADFILTER (1 << 2)
#define COERR_BADTYPE (1 << 3)

// CxMsg types (CxMsgType's result).
#define CXM_IEVENT (1 << 5)  // a routed InputEvent
#define CXM_COMMAND (1 << 6) // a broker command (appear/disappear/…)

// Broker commands (a CXM_COMMAND CxMsg's ID).
#define CXCMD_DISABLE 0x040d
#define CXCMD_ENABLE 0x040e
#define CXCMD_APPEAR 0x040b
#define CXCMD_DISAPPEAR 0x040c
#define CXCMD_KILL 0x040f
#define CXCMD_UNIQUE 0x0410

// The init record passed to CxBroker.
struct NewBroker {
    BYTE nb_Version;         // NB_VERSION
    STRPTR nb_Name;          // program identifier
    STRPTR nb_Title;         // displayed name
    STRPTR nb_Descr;         // description
    WORD nb_Unique;          // NBU_* uniqueness policy
    WORD nb_Flags;           // COF_* flags
    BYTE nb_Pri;             // broker priority
    struct MsgPort *nb_Port; // where CxMsgs are delivered
    WORD nb_ReservedChannel;
};

#define NB_VERSION 5

// nb_Unique / nb_Flags.
#define NBU_DUPLICATE 0
#define NBU_UNIQUE (1 << 0)
#define NBU_NOTIFY (1 << 1)
#define COF_SHOW_HIDE (1 << 2)

// A parsed input description (ParseIX / SetFilterIX).
struct InputXpression {
    BYTE ix_Version;    // IX_VERSION
    BYTE ix_Class;      // IECLASS_*
    UWORD ix_Code;      // matched code
    UWORD ix_CodeMask;  // significant ix_Code bits
    UWORD ix_Qualifier; // required qualifiers
    UWORD ix_QualMask;  // significant ix_Qualifier bits
    UWORD ix_QualSame;  // synonym-qualifier groups
};

#define IX_VERSION 2

// IX parse-error codes (ParseIX's result).
#define IXERR_OK 0
#define IXERR_NOMEM 1
#define IXERR_BUFFEROVERFLOW 2
#define IXERR_BADSPEC 4

// commodities.library's base.
struct CxBase {
    struct Library cx_LibNode;
};

#endif // LIBRARIES_COMMODITIES_H
