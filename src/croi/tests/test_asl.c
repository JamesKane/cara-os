// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(asl_alloc): L9.1 — asl.library requester lifecycle
// (docs/LEARGAS_ASL.md). asl is `syscall` flavour, so the impls are
// callable from the S-mode runner. Allocates a file + a font requester,
// checks the seeded defaults (drawer/file from the ASLFR_* tags), and
// frees them. The modal AslRequest arrives in L9.2.

#include <cara/asl_lib.h>
#include <cara/test.h>
#include <cara/types.h>
#include <libraries/asl.h>
#include <utility/tagitem.h>

static bool streq(const char *a, const char *b)
{
    if (!a || !b) {
        return a == b;
    }
    while (*a && *b) {
        if (*a != *b) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

KERNEL_TEST(asl_alloc)
{
    // File requester seeded with an initial drawer + file.
    struct TagItem ftags[] = {
        { ASLFR_TitleText, (IPTR)(uptr) "Pick a file" },
        { ASLFR_InitialDrawer, (IPTR)(uptr) "SYS:" },
        { ASLFR_InitialFile, (IPTR)(uptr) "startup" },
        { TAG_END, 0 },
    };
    struct FileRequester *fr =
        (struct FileRequester *)Croi_Asl_AllocAslRequest_Impl(ASL_FileRequest, ftags);
    TEST_ASSERT(ctx, fr != nullptr, "AllocAslRequest(file)");
    TEST_ASSERT(ctx, fr->rf_Dir && streq((const char *)fr->rf_Dir, "SYS:"), "initial drawer");
    TEST_ASSERT(ctx, fr->rf_File && streq((const char *)fr->rf_File, "startup"), "initial file");
    // The kernel-private view: type recovered, title stashed.
    struct CaraAslReq *req = (struct CaraAslReq *)fr; // pub at offset 0
    TEST_ASSERT(ctx, req->type == ASL_FileRequest, "type recovered from pub ptr");
    TEST_ASSERT(ctx, streq(req->title, "Pick a file"), "title stashed");
    Croi_Asl_FreeAslRequest_Impl(fr);

    // A file requester with no initial tags → empty (but non-null) fields.
    struct FileRequester *fr2 =
        (struct FileRequester *)Croi_Asl_AllocAslRequest_Impl(ASL_FileRequest, nullptr);
    TEST_ASSERT(ctx, fr2 != nullptr && fr2->rf_Dir && fr2->rf_Dir[0] == 0 && fr2->rf_File[0] == 0,
                "empty file requester");
    Croi_Asl_FreeFileRequest_Impl(fr2); // legacy free alias

    // A font requester.
    struct TagItem otags[] = { { ASLFO_TitleText, (IPTR)(uptr) "Pick a font" }, { TAG_END, 0 } };
    struct FontRequester *fo =
        (struct FontRequester *)Croi_Asl_AllocAslRequest_Impl(ASL_FontRequest, otags);
    TEST_ASSERT(ctx, fo != nullptr, "AllocAslRequest(font)");
    TEST_ASSERT(ctx, ((struct CaraAslReq *)fo)->type == ASL_FontRequest, "font type");
    Croi_Asl_FreeAslRequest_Impl(fo);

    // nullptr free is safe.
    Croi_Asl_FreeAslRequest_Impl(nullptr);
}
