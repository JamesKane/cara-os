// SPDX-License-Identifier: BSD-2-Clause
//
// Leargas LB — kernel-only heap path for OpenScreen / CloseScreen.
// Pulls in Croi_Alloc / Croi_Free which live in the kernel heap
// module; the dual-target screen.c carries everything else.

#include <cara/alloc.h>
#include <cara/dath.h>
#include <cara/leargas.h>
#include <cara/types.h>

[[nodiscard]] struct Screen *Leargas_OpenScreen(struct DathFramebuffer *fb, const char *title,
                                                DathColor pen0)
{
    if (!fb) {
        return nullptr;
    }
    struct LeargasScreen *s = (struct LeargasScreen *)Croi_Alloc(sizeof(struct LeargasScreen));
    if (!s) {
        return nullptr;
    }
    if (Leargas_Screen_InitInPlace(s, fb, title, pen0) != CARA_EOK) {
        Croi_Free(s);
        return nullptr;
    }
    Dath_Clear(fb, pen0);
    Leargas_Screen_SetActive(s);
    return &s->pub;
}

void Leargas_CloseScreen(struct Screen *pub)
{
    if (!pub) {
        return;
    }
    struct LeargasScreen *s = Leargas_Screen_FromPub(pub);
    if (Leargas_ActiveScreen() == pub) {
        Leargas_Screen_SetActive(nullptr);
    }
    Croi_Free(s);
}
