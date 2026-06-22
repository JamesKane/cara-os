// SPDX-License-Identifier: BSD-2-Clause
//
// iffparse.library handle lifecycle (L10.1, docs/IFFPARSE.md §2.1-2):
// AllocIFF/FreeIFF, InitIFFasDOS, OpenIFF/CloseIFF. The IFFHandle is a
// CaraIff on the SASOS shared heap whose public view (struct IFFHandle)
// sits at offset 0, so AllocIFF hands the caller &ci->pub and the impls
// recover CaraIff by cast. v0's only stream is a dos FileHandle bound via
// InitIFFasDOS + iff_Stream; the parse walk (ParseIFF, L10.2) and the
// write side (L10.3) read/write it via the dos impls.

#include <cara/alloc.h>   // Croi_Free
#include <cara/dos_lib.h> // Croi_Dos_Read/Seek_Impl
#include <cara/iffparse_lib.h>
#include <cara/shared.h> // Croi_AllocShared
#include <cara/types.h>
#include <dos/dos.h>
#include <exec/types.h>
#include <libraries/iffparse.h>

struct IFFHandle *Croi_Iff_AllocIFF_Impl(void)
{
    struct CaraIff *ci = (struct CaraIff *)Croi_AllocShared(sizeof(struct CaraIff));
    if (!ci) {
        return nullptr;
    }
    *ci = (struct CaraIff){ 0 };
    return &ci->pub;
}

void Croi_Iff_FreeIFF_Impl(struct IFFHandle *iff)
{
    if (iff) {
        // iff == &ci->pub == the CaraIff base (pub at offset 0).
        Croi_Free(iff);
    }
}

// InitIFFasDOS: mark the handle as DOS-stream flavoured. The client sets
// iff_Stream to its open dos FileHandle before OpenIFF.
void Croi_Iff_InitIFFasDOS_Impl(struct IFFHandle *iff)
{
    if (!iff) {
        return;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    ci->as_dos = true;
}

// OpenIFF: bind the stream (from iff_Stream) + record the mode + reset the
// context stack. v0 reads/validates nothing here — ParseIFF (L10.2) reads
// the FORM header; an empty/garbage file surfaces as IFFERR_NOTIFF then.
LONG Croi_Iff_OpenIFF_Impl(struct IFFHandle *iff, LONG rwMode)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (!ci->as_dos) {
        return IFFERR_NOHOOK; // only InitIFFasDOS streams in v0
    }
    ci->stream = (BPTR)(uptr)iff->iff_Stream;
    if (!ci->stream) {
        return IFFERR_NOHOOK;
    }
    ci->mode = (ULONG)(rwMode & IFFF_RWBITS);
    ci->depth = 0;
    ci->write_off = 0;
    ci->nstops = 0;
    ci->nexit = 0;
    ci->nprops = 0;
    ci->ncolls = 0;
    iff->iff_Depth = 0;
    iff->iff_Flags = (iff->iff_Flags & ~(ULONG)IFFF_RWBITS) | ci->mode;
    return 0;
}

// CloseIFF: drop the context stack. (The write-side flush lands in L10.3;
// the client owns + closes the dos FileHandle itself.)
void Croi_Iff_CloseIFF_Impl(struct IFFHandle *iff)
{
    if (!iff) {
        return;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    // Free the gathered props / collections (shared-heap copies).
    for (int i = 0; i < ci->nprops; i++) {
        if (ci->props[i].sp) {
            Croi_Free(ci->props[i].sp);
            ci->props[i].sp = nullptr;
        }
    }
    ci->nprops = 0;
    for (int i = 0; i < ci->ncolls; i++) {
        struct CollectionItem *it = ci->colls[i].head;
        while (it) {
            struct CollectionItem *next = it->ci_Next;
            Croi_Free(it);
            it = next;
        }
        ci->colls[i].head = nullptr;
    }
    ci->ncolls = 0;
    ci->depth = 0;
    ci->stream = (BPTR)0;
    iff->iff_Depth = 0;
}

// ---- The read walk (L10.2) ------------------------------------------

static int iff_read_be32(struct CaraIff *ci, ULONG *out)
{
    UBYTE b[4];
    if (Croi_Dos_Read_Impl(ci->stream, b, 4) != 4) {
        return 0;
    }
    *out = ((ULONG)b[0] << 24) | ((ULONG)b[1] << 16) | ((ULONG)b[2] << 8) | (ULONG)b[3];
    return 1;
}

static bool iff_is_stop(struct CaraIff *ci, LONG type, LONG id)
{
    for (int i = 0; i < ci->nstops; i++) {
        if (ci->stops[i].type == type && ci->stops[i].id == id) {
            return true;
        }
    }
    return false;
}

// One parse step: enter the next chunk (push a ContextNode, return 0) or
// IFFERR_EOF at the end of the top context. On re-entry it first skips
// the unread remainder + pad of the previously-entered sub-chunk.
static LONG iff_step(struct CaraIff *ci)
{
    if (ci->depth == 0) {
        // Enter the top FORM/LIST/CAT: header (id + size) + the group type.
        ULONG fid, fsize, ftype;
        if (!iff_read_be32(ci, &fid)) {
            return IFFERR_EOF;
        }
        if (fid != ID_FORM && fid != ID_LIST && fid != ID_CAT) {
            return IFFERR_NOTIFF;
        }
        if (!iff_read_be32(ci, &fsize) || !iff_read_be32(ci, &ftype)) {
            return IFFERR_MANGLED;
        }
        struct ContextNode *top = &ci->nodes[0];
        *top = (struct ContextNode){ 0 };
        top->cn_ID = (LONG)fid;
        top->cn_Type = (LONG)ftype;
        top->cn_Size = (LONG)fsize;
        top->cn_Scan = 4; // the 4-byte group type counts against the size
        ci->depth = 1;
    } else if (ci->depth >= 2) {
        // Skip the rest of the current sub-chunk + its pad, pop to the form.
        struct ContextNode *cur = &ci->nodes[ci->depth - 1];
        LONG remain = cur->cn_Size - cur->cn_Scan;
        LONG pad = cur->cn_Size & 1;
        if (remain + pad > 0) {
            if (Croi_Dos_Seek_Impl(ci->stream, remain + pad, OFFSET_CURRENT) < 0) {
                return IFFERR_SEEK;
            }
        }
        ci->nodes[0].cn_Scan += 8 + cur->cn_Size + pad; // header + body + pad
        ci->depth = 1;
    }

    // At form level: read the next sub-chunk header, or EOF at the end.
    struct ContextNode *top = &ci->nodes[0];
    if (top->cn_Scan >= top->cn_Size) {
        return IFFERR_EOF;
    }
    ULONG cid, csize;
    if (!iff_read_be32(ci, &cid)) {
        return IFFERR_EOF;
    }
    if (!iff_read_be32(ci, &csize)) {
        return IFFERR_MANGLED;
    }
    if (ci->depth >= CARA_IFF_MAXDEPTH) {
        return IFFERR_NOSCOPE;
    }
    struct ContextNode *sub = &ci->nodes[1];
    *sub = (struct ContextNode){ 0 };
    sub->cn_ID = (LONG)cid;
    sub->cn_Type = top->cn_Type;
    sub->cn_Size = (LONG)csize;
    sub->cn_Scan = 0;
    sub->cn_Node.mln_Pred = &top->cn_Node; // ParentChunk link
    ci->depth = 2;
    ci->pub.iff_Depth = ci->depth;
    return 0;
}

// If the just-entered chunk (depth-1) was registered with PropChunk /
// CollectionChunk, slurp its whole body off the stream into a shared-heap
// StoredProperty / CollectionItem and record it, then return true so the
// SCAN loop consumes it transparently. A prop replaces a prior copy; a
// collection prepends (FindCollection returns newest-first, like V36+).
static bool iff_gather(struct CaraIff *ci)
{
    if (ci->depth < 2) {
        return false;
    }
    struct ContextNode *cur = &ci->nodes[ci->depth - 1];
    LONG sz = cur->cn_Size;

    for (int i = 0; i < ci->nprops; i++) {
        if (ci->props[i].type == cur->cn_Type && ci->props[i].id == cur->cn_ID) {
            struct StoredProperty *sp = (struct StoredProperty *)Croi_AllocShared(
                sizeof(struct StoredProperty) + (sz > 0 ? sz : 0));
            if (!sp) {
                return false;
            }
            sp->sp_Size = sz;
            if (sz > 0 && Croi_Iff_ReadChunkBytes_Impl(&ci->pub, sp->sp_Data, sz) != sz) {
                Croi_Free(sp);
                return false;
            }
            if (ci->props[i].sp) {
                Croi_Free(ci->props[i].sp); // replace the prior copy
            }
            ci->props[i].sp = sp;
            return true;
        }
    }
    for (int i = 0; i < ci->ncolls; i++) {
        if (ci->colls[i].type == cur->cn_Type && ci->colls[i].id == cur->cn_ID) {
            struct CollectionItem *it = (struct CollectionItem *)Croi_AllocShared(
                sizeof(struct CollectionItem) + (sz > 0 ? sz : 0));
            if (!it) {
                return false;
            }
            it->ci_Size = sz;
            it->ci_Data = (UBYTE *)(it + 1);
            if (sz > 0 && Croi_Iff_ReadChunkBytes_Impl(&ci->pub, it->ci_Data, sz) != sz) {
                Croi_Free(it);
                return false;
            }
            it->ci_Next = ci->colls[i].head; // prepend (newest first)
            ci->colls[i].head = it;
            return true;
        }
    }
    return false;
}

LONG Croi_Iff_ParseIFF_Impl(struct IFFHandle *iff, LONG control)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (!ci->stream) {
        return IFFERR_NOHOOK;
    }
    for (;;) {
        LONG e = iff_step(ci);
        if (e != 0) {
            return e;
        }
        // Gather a registered prop / collection chunk (consumes it),
        // unless RAWSTEP suppresses gathering.
        bool gathered = (control != IFFPARSE_RAWSTEP) ? iff_gather(ci) : false;
        if (control != IFFPARSE_SCAN) {
            return 0; // STEP / RAWSTEP: one step (gathered or not)
        }
        if (gathered) {
            continue; // a prop/collection chunk — keep scanning
        }
        // SCAN: stop only when the entered chunk is registered.
        struct ContextNode *cur = &ci->nodes[ci->depth - 1];
        if (iff_is_stop(ci, cur->cn_Type, cur->cn_ID)) {
            return 0;
        }
        // else: keep stepping (the next iff_step skips this chunk).
    }
}

LONG Croi_Iff_StopChunk_Impl(struct IFFHandle *iff, LONG type, LONG id)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (ci->nstops >= CARA_IFF_MAXSTOPS) {
        return IFFERR_NOMEM;
    }
    ci->stops[ci->nstops].type = type;
    ci->stops[ci->nstops].id = id;
    ci->nstops++;
    return 0;
}

// v0: recorded but inert — the flat parser has no nested-context "exit"
// event for SCAN to act on (ILBM stops on entry via StopChunk).
LONG Croi_Iff_StopOnExit_Impl(struct IFFHandle *iff, LONG type, LONG id)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (ci->nexit >= CARA_IFF_MAXSTOPS) {
        return IFFERR_NOMEM;
    }
    ci->exit_stops[ci->nexit].type = type;
    ci->exit_stops[ci->nexit].id = id;
    ci->nexit++;
    return 0;
}

LONG Croi_Iff_ReadChunkBytes_Impl(struct IFFHandle *iff, APTR buf, LONG size)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (ci->depth < 2) {
        return IFFERR_NOSCOPE;
    }
    struct ContextNode *cur = &ci->nodes[ci->depth - 1];
    LONG avail = cur->cn_Size - cur->cn_Scan;
    if (size > avail) {
        size = avail; // clamp to the current chunk
    }
    if (size <= 0) {
        return 0;
    }
    LONG n = Croi_Dos_Read_Impl(ci->stream, buf, size);
    if (n < 0) {
        return IFFERR_READ;
    }
    cur->cn_Scan += n;
    return n;
}

LONG Croi_Iff_ReadChunkRecords_Impl(struct IFFHandle *iff, APTR buf, LONG recSize, LONG numRec)
{
    if (recSize <= 0) {
        return 0;
    }
    LONG bytes = Croi_Iff_ReadChunkBytes_Impl(iff, buf, recSize * numRec);
    if (bytes < 0) {
        return bytes;
    }
    return bytes / recSize;
}

struct ContextNode *Croi_Iff_CurrentChunk_Impl(struct IFFHandle *iff)
{
    if (!iff) {
        return nullptr;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (ci->depth == 0) {
        return nullptr;
    }
    return &ci->nodes[ci->depth - 1];
}

// The parent of a context node follows the cn_Node.mln_Pred link set at
// push time (cn_Node is at offset 0 of ContextNode); nullptr at the top.
struct ContextNode *Croi_Iff_ParentChunk_Impl(struct ContextNode *cn)
{
    if (!cn) {
        return nullptr;
    }
    return (struct ContextNode *)cn->cn_Node.mln_Pred;
}

// ---- The write side (L10.3) -----------------------------------------

static bool iff_write(struct CaraIff *ci, const void *buf, LONG n)
{
    if (Croi_Dos_Write_Impl(ci->stream, (APTR)(uptr)buf, n) != n) {
        return false;
    }
    ci->write_off += n;
    return true;
}

static bool iff_write_be32(struct CaraIff *ci, ULONG v)
{
    UBYTE b[4] = { (UBYTE)(v >> 24), (UBYTE)(v >> 16), (UBYTE)(v >> 8), (UBYTE)v };
    return iff_write(ci, b, 4);
}

// PushChunk(iff, type, id, size): open a new chunk for writing. A group
// (FORM/LIST/CAT/PROP) writes its id + size + the group type; a leaf
// writes its id + size. The size field offset is recorded in push_pos so
// PopChunk can backpatch the true size (IFFSIZE_UNKNOWN writes 0 now).
LONG Croi_Iff_PushChunk_Impl(struct IFFHandle *iff, LONG type, LONG id, LONG size)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (!ci->stream) {
        return IFFERR_NOHOOK;
    }
    if (ci->depth >= CARA_IFF_MAXDEPTH) {
        return IFFERR_NOSCOPE;
    }
    bool group = (id == ID_FORM || id == ID_LIST || id == ID_CAT || id == ID_PROP);
    LONG declared = (size == IFFSIZE_UNKNOWN) ? 0 : size;

    if (!iff_write_be32(ci, (ULONG)id)) {
        return IFFERR_WRITE;
    }
    ci->push_pos[ci->depth] = ci->write_off; // the size field is written next
    if (!iff_write_be32(ci, (ULONG)declared)) {
        return IFFERR_WRITE;
    }

    struct ContextNode *n = &ci->nodes[ci->depth];
    *n = (struct ContextNode){ 0 };
    n->cn_ID = id;
    n->cn_Size = declared;
    n->cn_Scan = 0;
    n->cn_Node.mln_Pred = (ci->depth >= 1) ? &ci->nodes[ci->depth - 1].cn_Node : nullptr;
    if (group) {
        if (!iff_write_be32(ci, (ULONG)type)) {
            return IFFERR_WRITE;
        }
        n->cn_Type = type;
        n->cn_Scan = 4; // the group type counts toward the content
    } else {
        n->cn_Type = (ci->depth >= 1) ? ci->nodes[ci->depth - 1].cn_Type : type;
    }
    ci->depth++;
    ci->pub.iff_Depth = ci->depth;
    return 0;
}

LONG Croi_Iff_WriteChunkBytes_Impl(struct IFFHandle *iff, APTR buf, LONG size)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (ci->depth < 1) {
        return IFFERR_NOSCOPE;
    }
    if (size <= 0) {
        return 0;
    }
    if (!iff_write(ci, buf, size)) {
        return IFFERR_WRITE;
    }
    ci->nodes[ci->depth - 1].cn_Scan += size;
    return size;
}

LONG Croi_Iff_WriteChunkRecords_Impl(struct IFFHandle *iff, APTR buf, LONG recSize, LONG numRec)
{
    if (recSize <= 0) {
        return 0;
    }
    LONG bytes = Croi_Iff_WriteChunkBytes_Impl(iff, buf, recSize * numRec);
    if (bytes < 0) {
        return bytes;
    }
    return bytes / recSize;
}

// PopChunk: close the current chunk — pad to even, backpatch its size
// field with the bytes actually written (cn_Scan), and account it against
// the parent so the enclosing FORM's size is correct.
LONG Croi_Iff_PopChunk_Impl(struct IFFHandle *iff)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    if (ci->depth < 1) {
        return IFFERR_NOSCOPE;
    }
    struct ContextNode *cur = &ci->nodes[ci->depth - 1];
    LONG sz = cur->cn_Scan;
    LONG pad = sz & 1;
    if (pad) {
        UBYTE z = 0;
        if (!iff_write(ci, &z, 1)) {
            return IFFERR_WRITE;
        }
    }
    // Backpatch the size field (overwrite the placeholder in place), then
    // return to the end. CaraFS Carafs_FileWrite overwrites at offset.
    LONG endpos = ci->write_off;
    if (Croi_Dos_Seek_Impl(ci->stream, ci->push_pos[ci->depth - 1], OFFSET_BEGINNING) < 0) {
        return IFFERR_SEEK;
    }
    UBYTE b[4] = { (UBYTE)((ULONG)sz >> 24), (UBYTE)((ULONG)sz >> 16), (UBYTE)((ULONG)sz >> 8),
                   (UBYTE)sz };
    if (Croi_Dos_Write_Impl(ci->stream, b, 4) != 4) {
        return IFFERR_WRITE;
    }
    if (Croi_Dos_Seek_Impl(ci->stream, endpos, OFFSET_BEGINNING) < 0) {
        return IFFERR_SEEK;
    }
    cur->cn_Size = sz;

    if (ci->depth >= 2) {
        // header (8) + body + pad counts against the enclosing context.
        ci->nodes[ci->depth - 2].cn_Scan += 8 + sz + pad;
    }
    ci->depth--;
    ci->pub.iff_Depth = ci->depth;
    return 0;
}

// ---- Props / collections (L10.4) ------------------------------------

// Register (type,id) so ParseIFF gathers it as a single StoredProperty
// (FindProp returns the latest copy). Must be called before ParseIFF.
LONG Croi_Iff_PropChunk_Impl(struct IFFHandle *iff, LONG type, LONG id)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    for (int i = 0; i < ci->nprops; i++) {
        if (ci->props[i].type == type && ci->props[i].id == id) {
            return 0; // already registered
        }
    }
    if (ci->nprops >= CARA_IFF_MAXSTOPS) {
        return IFFERR_NOMEM;
    }
    ci->props[ci->nprops].type = type;
    ci->props[ci->nprops].id = id;
    ci->props[ci->nprops].sp = nullptr;
    ci->nprops++;
    return 0;
}

// Register an array of (type,id) LONG pairs as prop chunks.
LONG Croi_Iff_PropChunks_Impl(struct IFFHandle *iff, LONG *propArray, LONG numPairs)
{
    if (!propArray) {
        return IFFERR_NOMEM;
    }
    for (LONG i = 0; i < numPairs; i++) {
        LONG e = Croi_Iff_PropChunk_Impl(iff, propArray[i * 2], propArray[i * 2 + 1]);
        if (e != 0) {
            return e;
        }
    }
    return 0;
}

// Register (type,id) so ParseIFF accumulates every matching chunk into a
// CollectionItem list (FindCollection returns the newest-first head).
LONG Croi_Iff_CollectionChunk_Impl(struct IFFHandle *iff, LONG type, LONG id)
{
    if (!iff) {
        return IFFERR_NOMEM;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    for (int i = 0; i < ci->ncolls; i++) {
        if (ci->colls[i].type == type && ci->colls[i].id == id) {
            return 0; // already registered
        }
    }
    if (ci->ncolls >= CARA_IFF_MAXSTOPS) {
        return IFFERR_NOMEM;
    }
    ci->colls[ci->ncolls].type = type;
    ci->colls[ci->ncolls].id = id;
    ci->colls[ci->ncolls].head = nullptr;
    ci->ncolls++;
    return 0;
}

// Register an array of (type,id) LONG pairs as collection chunks.
LONG Croi_Iff_CollectionChunks_Impl(struct IFFHandle *iff, LONG *array, LONG numPairs)
{
    if (!array) {
        return IFFERR_NOMEM;
    }
    for (LONG i = 0; i < numPairs; i++) {
        LONG e = Croi_Iff_CollectionChunk_Impl(iff, array[i * 2], array[i * 2 + 1]);
        if (e != 0) {
            return e;
        }
    }
    return 0;
}

// Return the gathered StoredProperty for (type,id), or nullptr if the
// chunk was never registered or did not appear in the parsed stream.
struct StoredProperty *Croi_Iff_FindProp_Impl(struct IFFHandle *iff, LONG type, LONG id)
{
    if (!iff) {
        return nullptr;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    for (int i = 0; i < ci->nprops; i++) {
        if (ci->props[i].type == type && ci->props[i].id == id) {
            return ci->props[i].sp;
        }
    }
    return nullptr;
}

// Return the head of the gathered CollectionItem list for (type,id).
struct CollectionItem *Croi_Iff_FindCollection_Impl(struct IFFHandle *iff, LONG type, LONG id)
{
    if (!iff) {
        return nullptr;
    }
    struct CaraIff *ci = (struct CaraIff *)iff;
    for (int i = 0; i < ci->ncolls; i++) {
        if (ci->colls[i].type == type && ci->colls[i].id == id) {
            return ci->colls[i].head;
        }
    }
    return nullptr;
}
