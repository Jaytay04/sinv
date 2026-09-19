#include "test_common.h"
#include "vmx_persistent_unload_policy.h"

typedef struct FIXTURE {
    VMX_PERSISTENT_UNLOAD_PHASE Phase, FailedPhase;
    int32_t StopStatus, DetachStatus, FailedStatus;
    uint32_t QueryFailures, DetachFailures;
    uint32_t QueryCalls, StopCalls, DetachCalls, DelayCalls, FailCalls;
    uint32_t FailedAttempts;
    bool DelaySucceeds;
} FIXTURE;

static int32_t Query(void* context, VMX_PERSISTENT_UNLOAD_PHASE* phase)
{
    FIXTURE* f = (FIXTURE*)context;
    f->QueryCalls++;
    if (f->QueryCalls <= f->QueryFailures)
        return -5;
    *phase = f->Phase;
    return 0;
}

static int32_t Stop(void* context)
{
    FIXTURE* f = (FIXTURE*)context;
    f->StopCalls++;
    return f->StopStatus;
}

static int32_t Detach(void* context)
{
    FIXTURE* f = (FIXTURE*)context;
    f->DetachCalls++;
    if (f->DetachCalls <= f->DetachFailures) {
        f->Phase = VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING;
        return f->DetachStatus;
    }
    f->Phase = VMX_PERSISTENT_UNLOAD_PHASE_DETACHED;
    return 0;
}

static bool Delay(void* context, uint32_t completed_attempt)
{
    FIXTURE* f = (FIXTURE*)context;
    f->DelayCalls++;
    TEST_ASSERT_UINT_EQ(completed_attempt, f->DelayCalls, "ordered delay attempt");
    return f->DelaySucceeds;
}

static void FailStop(void* context, int32_t status,
    VMX_PERSISTENT_UNLOAD_PHASE phase, uint32_t attempts)
{
    FIXTURE* f = (FIXTURE*)context;
    f->FailCalls++;
    f->FailedStatus = status;
    f->FailedPhase = phase;
    f->FailedAttempts = attempts;
}

static VMX_PERSISTENT_UNLOAD_PLATFORM Platform(FIXTURE* f)
{
    VMX_PERSISTENT_UNLOAD_PLATFORM p = {
        f, Query, Stop, Detach, Delay, FailStop
    };
    return p;
}

static FIXTURE Fixture(VMX_PERSISTENT_UNLOAD_PHASE phase)
{
    FIXTURE f = {0};
    f.Phase = phase;
    f.DetachStatus = -7;
    f.DelaySucceeds = true;
    return f;
}

static int TestDetachedAndAttachedSuccess(void)
{
    FIXTURE f = Fixture(VMX_PERSISTENT_UNLOAD_PHASE_DETACHED);
    VMX_PERSISTENT_UNLOAD_PLATFORM p = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 4), 0, "detached");
    TEST_ASSERT_UINT_EQ(f.QueryCalls, 1, "detached query");
    TEST_ASSERT_UINT_EQ(f.StopCalls + f.DetachCalls + f.DelayCalls + f.FailCalls,
        0, "detached has no side effects");

    f = Fixture(VMX_PERSISTENT_UNLOAD_PHASE_ATTACHED);
    p = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 4), 0, "attached cleanup");
    TEST_ASSERT_UINT_EQ(f.QueryCalls, 2, "pre/post proof");
    TEST_ASSERT_UINT_EQ(f.StopCalls, 1, "stop once");
    TEST_ASSERT_UINT_EQ(f.DetachCalls, 1, "detach once");
    TEST_ASSERT_UINT_EQ(f.DelayCalls + f.FailCalls, 0, "no needless retry");
    return 0;
}

static int TestFailureRecoveryAndPendingRetry(void)
{
    FIXTURE f = Fixture(VMX_PERSISTENT_UNLOAD_PHASE_ATTACHED);
    VMX_PERSISTENT_UNLOAD_PLATFORM p = Platform(&f);
    f.StopStatus = -6;
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 4), 0,
        "detached proof overrides prior stop failure");
    TEST_ASSERT_UINT_EQ(f.StopCalls, 1, "failed stop attempted");
    TEST_ASSERT_UINT_EQ(f.DetachCalls, 1, "destroy still attempted");

    f = Fixture(VMX_PERSISTENT_UNLOAD_PHASE_ATTACHED);
    f.DetachFailures = 2;
    p = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 4), 0, "pending retries");
    TEST_ASSERT_UINT_EQ(f.StopCalls, 1, "stop not replayed pending");
    TEST_ASSERT_UINT_EQ(f.DetachCalls, 3, "detach exact retries");
    TEST_ASSERT_UINT_EQ(f.DelayCalls, 2, "bounded delay between retries");
    TEST_ASSERT_UINT_EQ(f.FailCalls, 0, "recovery avoids fail-stop");
    return 0;
}

static int TestExhaustionAndDelayFailure(void)
{
    FIXTURE f = Fixture(VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING);
    VMX_PERSISTENT_UNLOAD_PLATFORM p;
    f.DetachFailures = 9;
    p = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 4), -7, "exhausted status");
    TEST_ASSERT_UINT_EQ(f.DetachCalls, 4, "bounded detach attempts");
    TEST_ASSERT_UINT_EQ(f.DelayCalls, 3, "no delay after final attempt");
    TEST_ASSERT_UINT_EQ(f.FailCalls, 1, "fail-stop once");
    TEST_ASSERT_INT_EQ(f.FailedStatus, -7, "exact fail-stop status");
    TEST_ASSERT_UINT_EQ(f.FailedPhase,
        VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING, "pending evidence");
    TEST_ASSERT_UINT_EQ(f.FailedAttempts, 4, "exact completed attempts");

    f = Fixture(VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING);
    f.DetachFailures = 9;
    f.DelaySucceeds = false;
    p = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 4), -7, "delay failure");
    TEST_ASSERT_UINT_EQ(f.DetachCalls, 1, "delay failure stops retry");
    TEST_ASSERT_UINT_EQ(f.FailCalls, 1, "delay failure fail-stop");
    TEST_ASSERT_UINT_EQ(f.FailedAttempts, 1, "one completed attempt");
    return 0;
}

static int TestQueryAndConfigFailures(void)
{
    FIXTURE f = Fixture(VMX_PERSISTENT_UNLOAD_PHASE_ATTACHED);
    VMX_PERSISTENT_UNLOAD_PLATFORM p;
    f.QueryFailures = 1;
    p = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 4), 0, "query retry");
    TEST_ASSERT_UINT_EQ(f.DelayCalls, 1, "query failure delayed");
    TEST_ASSERT_UINT_EQ(f.StopCalls, 1, "later stop");
    f = Fixture(VMX_PERSISTENT_UNLOAD_PHASE_INVALID); p = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 2),
        VMX_PERSISTENT_UNLOAD_POLICY_INVALID, "unknown phase");
    TEST_ASSERT_UINT_EQ(f.StopCalls + f.DetachCalls, 0, "unknown phase no teardown");
    TEST_ASSERT_UINT_EQ(f.FailCalls, 1, "unknown phase fail-stop");
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(NULL, 4),
        VMX_PERSISTENT_UNLOAD_POLICY_INVALID, "null platform");
    p = Platform(&f); p.FailStop = NULL;
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 4),
        VMX_PERSISTENT_UNLOAD_POLICY_INVALID, "missing fail-stop");
    p = Platform(&f);
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, 0),
        VMX_PERSISTENT_UNLOAD_POLICY_INVALID, "zero attempts");
    TEST_ASSERT_INT_EQ(VmxPersistentUnloadPolicyRun(&p, UINT32_MAX),
        VMX_PERSISTENT_UNLOAD_POLICY_INVALID, "attempt overflow");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestDetachedAndAttachedSuccess);
    TEST_RUN(TestFailureRecoveryAndPendingRetry);
    TEST_RUN(TestExhaustionAndDelayFailure);
    TEST_RUN(TestQueryAndConfigFailures);
TEST_MAIN_END()
