// SPDX-License-Identifier: BSD-2-Clause
//
// AmigaOS V36+ exec/interrupts.h — the canonical `struct Interrupt`
// (and `struct IntVector` / `struct SoftIntList`) every Release-2
// program targets (`<exec/interrupts.h>`). A `struct Interrupt` is the
// server node handed to AddIntServer / input.device IND_ADDHANDLER:
// is_Code is called with is_Data in a register.
//
// Field names, order, and offsets are V36+ ABI (exec/interrupts.h,
// Release 2.04, Revision 36.x) so V36+ source compiles unmodified.
//
// Source: AmigaOS RKM Libraries 3rd Edition (Exec interrupts) and the
// canonical <exec/interrupts.h> in the V36+ Includes.

#ifndef EXEC_INTERRUPTS_H
#define EXEC_INTERRUPTS_H

#include <exec/nodes.h>
#include <exec/types.h>

struct Interrupt {
    struct Node is_Node;
    APTR is_Data;          // server data segment passed to is_Code
    void (*is_Code)(void); // server code entry
};

#endif // EXEC_INTERRUPTS_H
