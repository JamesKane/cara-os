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
