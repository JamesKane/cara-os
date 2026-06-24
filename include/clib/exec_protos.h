// SPDX-License-Identifier: BSD-2-Clause
//
// <clib/exec_protos.h> — V36+ exec.library prototypes. Classic AmigaOS
// source includes <clib/X_protos.h> for the plain prototypes and links
// against amiga.lib's library-call stubs; CaraOS dispatches every library
// call through the generated <proto/X.h> inline stubs instead (LVO.md §5.2),
// which carry the same prototypes. So the clib header just forwards to the
// proto header — a V36+ program that includes either builds unmodified.

#ifndef CLIB_EXEC_PROTOS_H
#define CLIB_EXEC_PROTOS_H

#include <proto/exec.h>

#endif // CLIB_EXEC_PROTOS_H
