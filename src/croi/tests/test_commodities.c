// SPDX-License-Identifier: BSD-2-Clause
//
// KERNEL_TEST(commodities_tree): commodities.library's CxObj object model
// (L13.1) — build a broker tree, query it, and tear it down, all without
// any live input (the input-handler chain is deferred). Proves
// CreateCxObj/CxBroker/AttachCxObj/CxObjType/error/SetCxObjPri/Activate/
// Delete behave + the null-safety contract.

#include <cara/commodities_lib.h>
#include <cara/test.h>
#include <cara/types.h>
#include <libraries/commodities.h>

KERNEL_TEST(commodities_tree)
{
    struct NewBroker nb = {
        NB_VERSION, (STRPTR) "test", (STRPTR) "Test", (STRPTR) "desc", 0, 0, 5, nullptr, 0
    };
    LONG err = -1;
    struct CxObj *broker = Croi_Cx_CxBroker_Impl(&nb, &err);
    TEST_ASSERT(ctx, broker != nullptr, "CxBroker");
    TEST_ASSERT(ctx, err == 0, "broker error 0");
    TEST_ASSERT(ctx, Croi_Cx_CxObjType_Impl(broker) == CX_BROKER, "broker type");
    TEST_ASSERT(ctx, Croi_Cx_CxObjError_Impl(broker) == 0, "broker no error");

    struct CxObj *filt = Croi_Cx_CreateCxObj_Impl(CX_FILTER, (IPTR)(uptr) "f1", 0);
    struct CxObj *tr = Croi_Cx_CreateCxObj_Impl(CX_TRANSLATE, 0, 0);
    TEST_ASSERT(ctx, filt != nullptr && tr != nullptr, "CreateCxObj");
    TEST_ASSERT(ctx, Croi_Cx_CxObjType_Impl(filt) == CX_FILTER, "filter type");
    TEST_ASSERT(ctx, Croi_Cx_CxObjType_Impl(tr) == CX_TRANSLATE, "translate type");

    // SetCxObjPri returns the previous priority.
    TEST_ASSERT(ctx, Croi_Cx_SetCxObjPri_Impl(filt, 7) == 0, "old pri 0");
    TEST_ASSERT(ctx, Croi_Cx_SetCxObjPri_Impl(filt, 3) == 7, "old pri 7");

    // Build the tree: filter + translate under the broker.
    Croi_Cx_AttachCxObj_Impl(broker, filt);
    Croi_Cx_AttachCxObj_Impl(broker, tr);
    TEST_ASSERT(ctx, Croi_Cx_CxObjError_Impl(broker) == 0, "attach no error");

    // ActivateCxObj returns the previous active state.
    TEST_ASSERT(ctx, Croi_Cx_ActivateCxObj_Impl(broker, 1) == 0, "activate prev 0");
    TEST_ASSERT(ctx, Croi_Cx_ActivateCxObj_Impl(broker, 0) == 1, "activate prev 1");

    // A null attach flags COERR_NULLATTACH on the head; ClearCxObjError resets.
    Croi_Cx_AttachCxObj_Impl(broker, nullptr);
    TEST_ASSERT(ctx, (Croi_Cx_CxObjError_Impl(broker) & COERR_NULLATTACH) != 0,
                "null attach error");
    Croi_Cx_ClearCxObjError_Impl(broker);
    TEST_ASSERT(ctx, Croi_Cx_CxObjError_Impl(broker) == 0, "error cleared");

    // Unlink + delete one object, then the whole tree.
    Croi_Cx_RemoveCxObj_Impl(tr);
    Croi_Cx_DeleteCxObj_Impl(tr);
    Croi_Cx_DeleteCxObjAll_Impl(broker); // frees broker + filter

    // Null-safety contract.
    TEST_ASSERT(ctx, Croi_Cx_CxObjType_Impl(nullptr) == CX_INVALID, "null type");
    TEST_ASSERT(ctx, Croi_Cx_CxObjError_Impl(nullptr) == COERR_ISNULL, "null error");
}

// KERNEL_TEST(commodities_parseix): ParseIX + filter config + the CxMsg
// accessors (L13.2). Pure logic, no input plumbing.
KERNEL_TEST(commodities_parseix)
{
    struct InputXpression ix;
    TEST_ASSERT(ctx, Croi_Cx_ParseIX_Impl((STRPTR) "ctrl alt f1", &ix) == IXERR_OK, "ParseIX ok");
    TEST_ASSERT(ctx, ix.ix_Class == IECLASS_RAWKEY, "class rawkey");
    TEST_ASSERT(ctx, ix.ix_Code == 0x50, "code f1");
    TEST_ASSERT(ctx, ix.ix_Qualifier == (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT), "qualifiers");
    TEST_ASSERT(ctx, ix.ix_QualMask == (IEQUALIFIER_CONTROL | IEQUALIFIER_LALT), "qual mask");

    // An unrecognised token is a parse error (ascii keys are deferred).
    struct InputXpression bad;
    TEST_ASSERT(ctx, Croi_Cx_ParseIX_Impl((STRPTR) "ctrl boguskey", &bad) == IXERR_BADSPEC,
                "bad spec");

    // SetFilterIX stores a prebuilt IX; SetFilter parses + stores.
    struct CxObj *filt = Croi_Cx_CreateCxObj_Impl(CX_FILTER, 0, 0);
    TEST_ASSERT(ctx, filt != nullptr, "filter");
    TEST_ASSERT(ctx, Croi_Cx_SetFilterIX_Impl(filt, &ix) == 0, "SetFilterIX ok");
    Croi_Cx_SetFilter_Impl(filt, (STRPTR) "shift f2");
    TEST_ASSERT(ctx, Croi_Cx_CxObjError_Impl(filt) == 0, "SetFilter no error");
    Croi_Cx_DeleteCxObj_Impl(filt);

    // CxMsg accessors over an internally-built message.
    struct CxMsg *m = Croi_Cx_AllocCxMsg(CXM_IEVENT, (IPTR)0xCAFEu, 42);
    TEST_ASSERT(ctx, m != nullptr, "AllocCxMsg");
    TEST_ASSERT(ctx, Croi_Cx_CxMsgType_Impl(m) == CXM_IEVENT, "CxMsgType");
    TEST_ASSERT(ctx, Croi_Cx_CxMsgData_Impl(m) == (APTR)(uptr)0xCAFEu, "CxMsgData");
    TEST_ASSERT(ctx, Croi_Cx_CxMsgID_Impl(m) == 42, "CxMsgID");
    Croi_Cx_DisposeCxMsg_Impl(m);
}
