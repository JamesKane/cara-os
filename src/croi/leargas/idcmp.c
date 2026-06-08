// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LF — IDCMP key delivery (kernel-only). Holds the default
// key-routing hook the router calls, plus the consumer-side GetMsg /
// DisposeMsg. These touch the kernel heap (Croi_Alloc) and the
// MsgPort/Ring IPC layer (Croi_PutMsg / Croi_GetMsg), so unlike the
// dual-target translation in window.c they build only for the kernel.
//
// A window's UserPort is a `struct CroiMsgPort` whose public `struct
// MsgPort` prefix is its first field, so the `struct MsgPort *` stored
// in window->UserPort casts straight back to `struct CroiMsgPort *`.

#include <cara/alloc.h>
#include <cara/leargas.h>
#include <cara/msgport.h>
#include <cara/shared.h>
#include <cara/types.h>

[[nodiscard]] bool Leargas_IDCMP_RouteKey(struct Window *focused,
                                          const struct LeargasInputEvent *ev)
{
    if (!focused || !ev || !focused->UserPort) {
        return false;
    }
    if (!(focused->IDCMPFlags & IDCMP_RAWKEY)) {
        return false; // the window didn't ask for keys
    }

    struct IntuiMessage *im = (struct IntuiMessage *)Croi_AllocShared(sizeof(struct IntuiMessage));
    if (!im) {
        return false;
    }
    *im = (struct IntuiMessage){ 0 };
    Leargas_BuildIntuiMessage(im, focused, ev);

    struct CroiMsgPort *port = (struct CroiMsgPort *)focused->UserPort;
    struct RingSlot slot = {
        .kind = IDCMP_RAWKEY,
        .length = (u32)sizeof(struct IntuiMessage),
        .payload = (uptr)im,
        .reserved = 0,
    };
    if (!Croi_PutMsg(port, slot)) {
        // Ring full — the client isn't draining fast enough. Drop the
        // event rather than block the input path, and free the message
        // we couldn't enqueue.
        Croi_Free(im);
        return false;
    }
    return true;
}

[[nodiscard]] bool Leargas_IDCMP_GetMsg(struct Window *w, struct IntuiMessage **out)
{
    if (!w || !w->UserPort || !out) {
        return false;
    }
    struct CroiMsgPort *port = (struct CroiMsgPort *)w->UserPort;
    struct RingSlot slot;
    if (!Croi_GetMsg(port, &slot)) {
        return false;
    }
    *out = (struct IntuiMessage *)slot.payload;
    return true;
}

void Leargas_IDCMP_DisposeMsg(struct IntuiMessage *im)
{
    if (im) {
        Croi_Free(im);
    }
}

[[nodiscard]] bool Leargas_IDCMP_PostMouseButtons(struct Window *w,
                                                  const struct LeargasInputEvent *ev)
{
    if (!w || !ev || !w->UserPort) {
        return false;
    }
    if (!(w->IDCMPFlags & IDCMP_MOUSEBUTTONS)) {
        return false; // the window didn't ask for button events
    }

    struct IntuiMessage *im = (struct IntuiMessage *)Croi_AllocShared(sizeof(struct IntuiMessage));
    if (!im) {
        return false;
    }
    *im = (struct IntuiMessage){ 0 };
    // BuildIntuiMessage maps the class (IECLASS_RAWMOUSE → MOUSEBUTTONS)
    // and copies Code (ie_code is SELECTDOWN/SELECTUP), Qualifier, and
    // the window-relative MouseX/Y.
    Leargas_BuildIntuiMessage(im, w, ev);

    struct CroiMsgPort *port = (struct CroiMsgPort *)w->UserPort;
    struct RingSlot slot = {
        .kind = IDCMP_MOUSEBUTTONS,
        .length = (u32)sizeof(struct IntuiMessage),
        .payload = (uptr)im,
        .reserved = 0,
    };
    if (!Croi_PutMsg(port, slot)) {
        Croi_Free(im);
        return false;
    }
    return true;
}

[[nodiscard]] bool Leargas_IDCMP_PostCloseWindow(struct Window *w)
{
    if (!w || !w->UserPort) {
        return false;
    }
    if (!(w->IDCMPFlags & IDCMP_CLOSEWINDOW)) {
        return false; // the window didn't ask for close events
    }

    struct IntuiMessage *im = (struct IntuiMessage *)Croi_AllocShared(sizeof(struct IntuiMessage));
    if (!im) {
        return false;
    }
    *im = (struct IntuiMessage){ 0 };
    im->ExecMessage.mn_Length = (UWORD)sizeof(struct IntuiMessage);
    im->Class = IDCMP_CLOSEWINDOW;
    im->IDCMPWindow = w;
    if (w->WScreen) {
        im->MouseX = (WORD)(w->WScreen->MouseX - w->LeftEdge);
        im->MouseY = (WORD)(w->WScreen->MouseY - w->TopEdge);
    }

    struct CroiMsgPort *port = (struct CroiMsgPort *)w->UserPort;
    struct RingSlot slot = {
        .kind = IDCMP_CLOSEWINDOW,
        .length = (u32)sizeof(struct IntuiMessage),
        .payload = (uptr)im,
        .reserved = 0,
    };
    if (!Croi_PutMsg(port, slot)) {
        Croi_Free(im);
        return false;
    }
    return true;
}

[[nodiscard]] bool Leargas_IDCMP_PostGadgetUp(struct Window *w, struct Gadget *g)
{
    if (!w || !g || !w->UserPort) {
        return false;
    }
    if (!(w->IDCMPFlags & IDCMP_GADGETUP)) {
        return false; // the window didn't ask for gadget events
    }

    struct IntuiMessage *im = (struct IntuiMessage *)Croi_AllocShared(sizeof(struct IntuiMessage));
    if (!im) {
        return false;
    }
    *im = (struct IntuiMessage){ 0 };
    im->ExecMessage.mn_Length = (UWORD)sizeof(struct IntuiMessage);
    im->Class = IDCMP_GADGETUP;
    im->Code = g->GadgetID; // the doc's "GADGETUP with the gadget's GadgetID"
    im->IAddress = g;
    im->IDCMPWindow = w;
    if (w->WScreen) {
        im->MouseX = (WORD)(w->WScreen->MouseX - w->LeftEdge);
        im->MouseY = (WORD)(w->WScreen->MouseY - w->TopEdge);
    }

    struct CroiMsgPort *port = (struct CroiMsgPort *)w->UserPort;
    struct RingSlot slot = {
        .kind = IDCMP_GADGETUP,
        .length = (u32)sizeof(struct IntuiMessage),
        .payload = (uptr)im,
        .reserved = 0,
    };
    if (!Croi_PutMsg(port, slot)) {
        Croi_Free(im);
        return false;
    }
    return true;
}
