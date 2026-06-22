// SPDX-License-Identifier: BSD-2-Clause
//
// asl.library requester lifecycle (L9.1, docs/LEARGAS_ASL.md §2.3):
// AllocAslRequest / FreeAslRequest (+ the legacy FreeFileRequest). The
// requester is a CaraAslReq on the SASOS shared heap whose public view
// (FileRequester / FontRequester / ScreenModeRequester) sits at offset 0,
// so the caller's opaque APTR is &req->pub and we recover the CaraAslReq
// by casting it back. Config tags are parsed into the kernel-private tail
// + the owned path buffers; AslRequest (L9.2) runs the modal loop.

#include <cara/alloc.h> // Croi_Free
#include <cara/asl_lib.h>
#include <cara/gadtools_lib.h>  // Croi_GT_* (the requester gadgets)
#include <cara/intuition_lib.h> // Croi_CloseWindow_Impl
#include <cara/leargas.h>       // Leargas_OpenWindow / IDCMP / AddGadget
#include <cara/sched.h>         // Croi_Wait
#include <cara/shared.h>        // Croi_AllocShared
#include <cara/tagitem.h>       // Croi_GetTagData
#include <cara/types.h>
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>

// Copy at most max-1 bytes of src into dst, NUL-terminating (src may be
// null → empty).
static void asl_strcopy(char *dst, const char *src, int max)
{
    int n = 0;
    if (src) {
        while (src[n] && n < max - 1) {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = '\0';
}

APTR Croi_Asl_AllocAslRequest_Impl(ULONG reqType, struct TagItem *tags)
{
    struct CaraAslReq *req = (struct CaraAslReq *)Croi_AllocShared(sizeof(struct CaraAslReq));
    if (!req) {
        return nullptr;
    }
    *req = (struct CaraAslReq){ 0 };
    req->type = reqType;

    switch (reqType) {
    case ASL_FileRequest:
        // Owned dir/file buffers, seeded from the initial-path tags.
        asl_strcopy(req->dirbuf, (const char *)(uptr)Croi_GetTagData(tags, ASLFR_InitialDrawer, 0),
                    CARA_ASL_DIRMAX);
        asl_strcopy(req->filebuf, (const char *)(uptr)Croi_GetTagData(tags, ASLFR_InitialFile, 0),
                    CARA_ASL_FILEMAX);
        req->pub.file.rf_Dir = req->dirbuf;
        req->pub.file.rf_File = req->filebuf;
        req->title = (const char *)(uptr)Croi_GetTagData(tags, ASLFR_TitleText, 0);
        req->parent = (struct Window *)(uptr)Croi_GetTagData(tags, ASLFR_Window, 0);
        break;
    case ASL_FontRequest:
        req->title = (const char *)(uptr)Croi_GetTagData(tags, ASLFO_TitleText, 0);
        req->parent = (struct Window *)(uptr)Croi_GetTagData(tags, ASLFO_Window, 0);
        break;
    case ASL_ScreenModeRequest:
        req->title = (const char *)(uptr)Croi_GetTagData(tags, ASLSM_TitleText, 0);
        req->parent = (struct Window *)(uptr)Croi_GetTagData(tags, ASLSM_Window, 0);
        break;
    default:
        // Unknown type — still a valid (empty) requester; AslRequest
        // will reject it.
        break;
    }
    return &req->pub;
}

void Croi_Asl_FreeAslRequest_Impl(APTR requester)
{
    if (requester) {
        // requester == &req->pub == the CaraAslReq base (pub at offset 0).
        Croi_Free(requester);
    }
}

// Legacy V36 alias.
void Croi_Asl_FreeFileRequest_Impl(struct FileRequester *fileReq)
{
    Croi_Asl_FreeAslRequest_Impl(fileReq);
}

// ---- The modal file requester (L9.2) --------------------------------

static void asl_strcopy_n(char *dst, const char *src, int max)
{
    int n = 0;
    if (src) {
        while (src[n] && n < max - 1) {
            dst[n] = src[n];
            n++;
        }
    }
    dst[n] = '\0';
}

// Build the modal window for a file requester: a drawer + a file STRING
// gadget (seeded from req->dirbuf/filebuf) and OK/Cancel buttons, over
// the L8 gadtools kinds. Stores the window + gadgets in `req`. Returns
// the window, or nullptr. (Font/screen-mode requesters arrive in L9.3.)
struct Window *Croi_Asl_Build(struct CaraAslReq *req, struct TagItem *tags)
{
    if (!req || req->type != ASL_FileRequest) {
        return nullptr;
    }
    // Call-time tags override the alloc-time initial path / title.
    const char *d = (const char *)(uptr)Croi_GetTagData(tags, ASLFR_InitialDrawer, 0);
    if (d) {
        asl_strcopy_n(req->dirbuf, d, CARA_ASL_DIRMAX);
    }
    const char *f = (const char *)(uptr)Croi_GetTagData(tags, ASLFR_InitialFile, 0);
    if (f) {
        asl_strcopy_n(req->filebuf, f, CARA_ASL_FILEMAX);
    }
    const char *t = (const char *)(uptr)Croi_GetTagData(tags, ASLFR_TitleText, (IPTR)req->title);

    struct Screen *scr = req->parent && req->parent->WScreen ? req->parent->WScreen
                                                             : Leargas_ActiveScreen();
    if (!scr) {
        return nullptr;
    }
    WORD w = 240, h = 80;
    WORD sx = (WORD)((scr->Width - w) / 2);
    WORD sy = (WORD)((scr->Height - h) / 2);
    if (sx < 0) {
        sx = 0;
    }
    if (sy < 0) {
        sy = 0;
    }
    struct NewWindow nw = (struct NewWindow){ 0 };
    nw.LeftEdge = sx;
    nw.TopEdge = sy;
    nw.Width = w;
    nw.Height = h;
    nw.DetailPen = 0;
    nw.BlockPen = 1;
    nw.IDCMPFlags = IDCMP_GADGETUP;
    nw.Flags = WFLG_DRAGBAR | WFLG_ACTIVATE;
    nw.Title = (UBYTE *)(uptr)(t ? t : "Select a File");
    struct Window *win = Leargas_OpenWindow(&nw);
    if (!win) {
        return nullptr;
    }

    APTR vi = Croi_GT_GetVisualInfoA_Impl(scr, nullptr);
    struct Gadget *glist = nullptr;
    struct Gadget *prev = Croi_GT_CreateContext_Impl(&glist);

    struct NewGadget ng = (struct NewGadget){ 0 };
    ng.ng_VisualInfo = vi;
    // Drawer field.
    ng.ng_LeftEdge = 8;
    ng.ng_TopEdge = 14;
    ng.ng_Width = (WORD)(w - 16);
    ng.ng_Height = 12;
    ng.ng_GadgetID = 10;
    struct TagItem dtags[] = { { GTST_String, (IPTR)(uptr)req->dirbuf },
                               { GTST_MaxChars, CARA_ASL_DIRMAX - 1 },
                               { TAG_END, 0 } };
    struct Gadget *gd = Croi_GT_CreateGadgetA_Impl(STRING_KIND, prev, &ng, dtags);
    // File field.
    ng.ng_TopEdge = 30;
    ng.ng_GadgetID = 11;
    struct TagItem ftags[] = { { GTST_String, (IPTR)(uptr)req->filebuf },
                               { GTST_MaxChars, CARA_ASL_FILEMAX - 1 },
                               { TAG_END, 0 } };
    struct Gadget *gf = Croi_GT_CreateGadgetA_Impl(STRING_KIND, gd, &ng, ftags);
    // OK (id 1) + Cancel (id 0) buttons.
    ng.ng_TopEdge = (WORD)(h - 18);
    ng.ng_Width = 64;
    ng.ng_Height = 14;
    ng.ng_LeftEdge = 8;
    ng.ng_GadgetID = 1;
    ng.ng_GadgetText = (STRPTR) "OK";
    struct Gadget *gok = Croi_GT_CreateGadgetA_Impl(BUTTON_KIND, gf, &ng, nullptr);
    ng.ng_LeftEdge = (WORD)(w - 72);
    ng.ng_GadgetID = 0;
    ng.ng_GadgetText = (STRPTR) "Cancel";
    (void)Croi_GT_CreateGadgetA_Impl(BUTTON_KIND, gok, &ng, nullptr);

    // The gadgets are chained context→gd→gf→gok→cancel; adding gd brings
    // the whole chain onto the window.
    if (gd) {
        Leargas_AddGadget(win, gd);
    }
    Leargas_Window_RenderGadgets(win);

    req->win = win;
    req->glist = glist;
    req->vi = vi;
    req->g_drawer = gd;
    req->g_file = gf;
    req->g_ok = gok;
    return win;
}

// Read a gadtools STRING gadget's edited buffer (SpecialInfo is the
// StringInfo at offset 0).
static const char *asl_gadget_str(struct Gadget *g)
{
    if (!g || !g->SpecialInfo) {
        return nullptr;
    }
    return (const char *)((struct StringInfo *)g->SpecialInfo)->Buffer;
}

// Run the modal loop: block on the window IDCMP port, exit on OK (id 1)
// or Cancel (id 0); string-field Returns are ignored. On OK copy the
// edited fields back into rf_Dir/rf_File. Tears down the window/gadgets.
BOOL Croi_Asl_Wait(struct CaraAslReq *req)
{
    if (!req || !req->win) {
        return FALSE;
    }
    struct Window *win = req->win;
    struct LeargasWindow *lw = Leargas_Window_FromPub(win);
    i32 sig = lw ? lw->idcmp_sigbit : -1;
    BOOL result = FALSE;
    bool done = false;
    while (!done) {
        if (sig >= 0) {
            (void)Croi_Wait(1u << (u32)sig);
        }
        struct IntuiMessage *im;
        while (Leargas_IDCMP_GetMsg(win, &im)) {
            if (im->Class == IDCMP_GADGETUP) {
                struct Gadget *g = (struct Gadget *)im->IAddress;
                UWORD id = g ? g->GadgetID : (UWORD)im->Code;
                if (id == 1) { // OK
                    asl_strcopy_n(req->dirbuf, asl_gadget_str(req->g_drawer), CARA_ASL_DIRMAX);
                    asl_strcopy_n(req->filebuf, asl_gadget_str(req->g_file), CARA_ASL_FILEMAX);
                    result = TRUE;
                    done = true;
                } else if (id == 0) { // Cancel
                    result = FALSE;
                    done = true;
                }
                // else: a STRING field's Return — keep editing.
            }
            Leargas_IDCMP_DisposeMsg(im);
        }
        if (sig < 0) {
            break; // no port to block on — avoid spinning
        }
    }
    Croi_CloseWindow_Impl(win);
    Croi_GT_FreeGadgets_Impl(req->glist);
    Croi_GT_FreeVisualInfo_Impl(req->vi);
    req->win = nullptr;
    req->glist = nullptr;
    req->vi = nullptr;
    req->g_drawer = nullptr;
    req->g_file = nullptr;
    req->g_ok = nullptr;
    return result;
}

BOOL Croi_Asl_AslRequest_Impl(APTR requester, struct TagItem *tags)
{
    struct CaraAslReq *req = (struct CaraAslReq *)requester;
    if (!req) {
        return FALSE;
    }
    if (!Croi_Asl_Build(req, tags)) {
        return FALSE; // unsupported type (font/screen-mode are L9.3)
    }
    return Croi_Asl_Wait(req);
}

// Legacy V36 wrappers.
APTR Croi_Asl_AllocFileRequest_Impl(void)
{
    return Croi_Asl_AllocAslRequest_Impl(ASL_FileRequest, nullptr);
}

BOOL Croi_Asl_RequestFile_Impl(struct FileRequester *fileReq)
{
    return Croi_Asl_AslRequest_Impl(fileReq, nullptr);
}
