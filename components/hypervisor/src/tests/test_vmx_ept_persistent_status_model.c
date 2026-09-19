#include "test_common.h"
#include "vmx_ept_persistent_status_model.h"

static VMX_EPT_PERSISTENT_STATUS_VIEW View(uint32_t phase,
    uint32_t stage, uint32_t zero)
{
    VMX_EPT_PERSISTENT_STATUS_VIEW view = {0};
    view.Version = VMX_EPT_PERSISTENT_STATUS_MODEL_VERSION;
    view.Size = sizeof(view);
    view.RuntimePhase = phase;
    view.EptStage = stage;
    view.EptZero = zero;
    return view;
}

static int ExpectClass(VMX_EPT_PERSISTENT_STATUS_VIEW* view,
    uint32_t classification, uint32_t required_flags)
{
    VMX_EPT_PERSISTENT_STATUS_RECORD record = {0};
    TEST_ASSERT_TRUE(VmxEptPersistentStatusModelProject(view, &record), "project");
    TEST_ASSERT_UINT_EQ(record.Version, VMX_EPT_PERSISTENT_STATUS_MODEL_VERSION,
        "record version");
    TEST_ASSERT_UINT_EQ(record.Size, sizeof(record), "record size");
    TEST_ASSERT_UINT_EQ(record.Classification, classification, "classification");
    TEST_ASSERT_UINT_EQ(record.Flags & required_flags, required_flags, "flags");
    return 0;
}

static int TestCanonicalStates(void)
{
    VMX_EPT_PERSISTENT_STATUS_VIEW view = View(
        VMX_EPT_PERSISTENT_STATUS_PHASE_DETACHED,
        VMX_EPT_PERSISTENT_STATUS_STAGE_ZERO, 1);
    TEST_ASSERT_INT_EQ(ExpectClass(&view, VMX_EPT_PERSISTENT_STATUS_DETACHED,
        VMX_EPT_PERSISTENT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        VMX_EPT_PERSISTENT_STATUS_FLAG_EPT_ZERO), 0, "detached");
    view = View(VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED,
        VMX_EPT_PERSISTENT_STATUS_STAGE_READY, 0);
    TEST_ASSERT_INT_EQ(ExpectClass(&view, VMX_EPT_PERSISTENT_STATUS_READY,
        VMX_EPT_PERSISTENT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        VMX_EPT_PERSISTENT_STATUS_FLAG_EPT_AUTHENTICATED), 0, "ready");
    view = View(VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED,
        VMX_EPT_PERSISTENT_STATUS_STAGE_SESSION_ONLY, 0);
    TEST_ASSERT_INT_EQ(ExpectClass(&view,
        VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING,
        VMX_EPT_PERSISTENT_STATUS_FLAG_CLEANUP_REQUIRED), 0, "session-only");
    view = View(VMX_EPT_PERSISTENT_STATUS_PHASE_TEARDOWN_PENDING,
        VMX_EPT_PERSISTENT_STATUS_STAGE_COMPOSITION_GONE, 0);
    TEST_ASSERT_INT_EQ(ExpectClass(&view,
        VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING,
        VMX_EPT_PERSISTENT_STATUS_FLAG_CLEANUP_REQUIRED), 0, "composition gone");
    view = View(VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED,
        VMX_EPT_PERSISTENT_STATUS_STAGE_BLOCKED, 0);
    TEST_ASSERT_INT_EQ(ExpectClass(&view, VMX_EPT_PERSISTENT_STATUS_BLOCKED,
        VMX_EPT_PERSISTENT_STATUS_FLAG_CLEANUP_REQUIRED), 0, "blocked");
    view = View(VMX_EPT_PERSISTENT_STATUS_PHASE_TEARDOWN_PENDING,
        VMX_EPT_PERSISTENT_STATUS_STAGE_ZERO, 1);
    TEST_ASSERT_INT_EQ(ExpectClass(&view,
        VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING,
        VMX_EPT_PERSISTENT_STATUS_FLAG_EPT_ZERO |
        VMX_EPT_PERSISTENT_STATUS_FLAG_CLEANUP_REQUIRED), 0, "zero pending");
    return 0;
}

static int TestMalformedAndUnavailable(void)
{
    VMX_EPT_PERSISTENT_STATUS_VIEW view = View(
        VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED,
        VMX_EPT_PERSISTENT_STATUS_STAGE_ZERO, 1);
    TEST_ASSERT_INT_EQ(ExpectClass(&view, VMX_EPT_PERSISTENT_STATUS_MALFORMED,
        VMX_EPT_PERSISTENT_STATUS_FLAG_EPT_ZERO), 0, "zero attached contradiction");
    view = View(VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED,
        VMX_EPT_PERSISTENT_STATUS_STAGE_READY, 0);
    view.EptValidateStatus = -1;
    TEST_ASSERT_INT_EQ(ExpectClass(&view, VMX_EPT_PERSISTENT_STATUS_MALFORMED,
        VMX_EPT_PERSISTENT_STATUS_FLAG_RUNTIME_AUTHENTICATED), 0, "invalid EPT");
    view = View(99, VMX_EPT_PERSISTENT_STATUS_STAGE_READY, 0);
    TEST_ASSERT_INT_EQ(ExpectClass(&view, VMX_EPT_PERSISTENT_STATUS_MALFORMED,
        VMX_EPT_PERSISTENT_STATUS_FLAG_RUNTIME_AUTHENTICATED), 0, "invalid phase");
    view = View(VMX_EPT_PERSISTENT_STATUS_PHASE_UNINITIALIZED,
        VMX_EPT_PERSISTENT_STATUS_STAGE_ZERO, 1);
    view.PhaseQueryStatus = -5;
    TEST_ASSERT_INT_EQ(ExpectClass(&view, VMX_EPT_PERSISTENT_STATUS_BLOCKED,
        VMX_EPT_PERSISTENT_STATUS_FLAG_CLEANUP_REQUIRED), 0, "query unavailable");
    return 0;
}

static int TestEvidenceAndStructuralRejection(void)
{
    VMX_EPT_PERSISTENT_STATUS_VIEW view = View(
        VMX_EPT_PERSISTENT_STATUS_PHASE_ATTACHED,
        VMX_EPT_PERSISTENT_STATUS_STAGE_READY, 0);
    VMX_EPT_PERSISTENT_STATUS_RECORD record = {0};
    view.StartEvidenceValid = view.DestroyEvidenceValid = 1;
    view.Start.AttachCalls = view.Start.StartCalls =
        view.Start.EptInitializeCalls = 1;
    view.Start.AttachStatus = 7;
    view.Start.DiagnosticValid = 1;
    view.Start.DiagnosticKind = 11;
    view.Start.DiagnosticSystemIndex = 65;
    view.Start.DiagnosticGroup = 1;
    view.Start.DiagnosticNumber = 1;
    view.Destroy.CompositionDestroyCalls = view.Destroy.DetachCalls = 1;
    view.Destroy.DetachStatus = -6;
    TEST_ASSERT_TRUE(VmxEptPersistentStatusModelProject(&view, &record), "evidence");
    TEST_ASSERT_UINT_EQ(record.Flags &
        (VMX_EPT_PERSISTENT_STATUS_FLAG_START_EVIDENCE |
         VMX_EPT_PERSISTENT_STATUS_FLAG_DESTROY_EVIDENCE),
        VMX_EPT_PERSISTENT_STATUS_FLAG_START_EVIDENCE |
        VMX_EPT_PERSISTENT_STATUS_FLAG_DESTROY_EVIDENCE, "evidence flags");
    TEST_ASSERT_INT_EQ(record.Start.AttachStatus, 7, "start evidence copied");
    TEST_ASSERT_INT_EQ(record.Destroy.DetachStatus, -6, "destroy evidence copied");

    record.Version = 77;
    view.Reserved[0] = 1;
    TEST_ASSERT_FALSE(VmxEptPersistentStatusModelProject(&view, &record),
        "reserved rejected");
    TEST_ASSERT_UINT_EQ(record.Version, 77, "invalid preserves output");
    view.Reserved[0] = 0;
    record = (VMX_EPT_PERSISTENT_STATUS_RECORD){0};
    view.Start.AttachCalls = 2;
    TEST_ASSERT_FALSE(VmxEptPersistentStatusModelProject(&view, &record),
        "call count rejected");
    view.Start.AttachCalls = 1;
    view.StartEvidenceValid = 0;
    TEST_ASSERT_FALSE(VmxEptPersistentStatusModelProject(&view, &record),
        "hidden evidence rejected");
    TEST_ASSERT_FALSE(VmxEptPersistentStatusModelProject(NULL, &record), "null view");
    TEST_ASSERT_FALSE(VmxEptPersistentStatusModelProject(&view, NULL), "null output");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestCanonicalStates);
    TEST_RUN(TestMalformedAndUnavailable);
    TEST_RUN(TestEvidenceAndStructuralRejection);
TEST_MAIN_END()
