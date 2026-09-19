#include "test_common.h"
#include "vmx.h"
#include "vmx_platform_readiness.h"

#define VALID_VMX_BASIC (UINT64_C(0x123) | \
    ((uint64_t)VMX_REGION_MIN_SIZE << VMX_BASIC_VMCS_SIZE_SHIFT) | \
    ((uint64_t)VMX_BASIC_MEMORY_TYPE_WB << VMX_BASIC_MEMORY_TYPE_SHIFT))

static VMX_PLATFORM_READINESS_SAMPLE ValidSample(uint64_t VmxBasic)
{
    VMX_PLATFORM_READINESS_SAMPLE Sample = { 0 };
    Sample.CpuidVmx = 1;
    Sample.FeatureControl = FEATURE_CONTROL_LOCK_BIT |
        FEATURE_CONTROL_VMXON_OUTSIDE_SMX;
    Sample.VmxBasic = VmxBasic;
    Sample.VmxMisc = UINT64_C(5);
    Sample.PinControls =
        (uint64_t)PIN_BASED_ACTIVATE_PREEMPTION_TIMER << 32;
    return Sample;
}

static int TestValidSample(void)
{
    VMX_PLATFORM_READINESS_SAMPLE Sample = ValidSample(VALID_VMX_BASIC);
    VMX_PLATFORM_READINESS_REASON Reason = VMX_PLATFORM_READINESS_REASON_RESERVED;
    TEST_ASSERT(VmxPlatformReadinessSampleValidate(&Sample, &Reason),
        "valid sample admitted");
    TEST_ASSERT_UINT_EQ(Reason, VMX_PLATFORM_READINESS_REASON_NONE,
        "valid reason");
    return 0;
}

static int TestInvalidSamples(void)
{
    VMX_PLATFORM_READINESS_SAMPLE Sample;
    VMX_PLATFORM_READINESS_REASON Reason;

    Sample = ValidSample(VALID_VMX_BASIC); Sample.Reserved = 1;
    TEST_ASSERT(!VmxPlatformReadinessSampleValidate(&Sample, &Reason),
        "reserved sample rejected");
    TEST_ASSERT_UINT_EQ(Reason, VMX_PLATFORM_READINESS_REASON_RESERVED,
        "reserved reason");
    Sample = ValidSample(VALID_VMX_BASIC); Sample.CpuidVmx = 0;
    TEST_ASSERT(!VmxPlatformReadinessSampleValidate(&Sample, &Reason),
        "missing VMX rejected");
    TEST_ASSERT_UINT_EQ(Reason, VMX_PLATFORM_READINESS_REASON_CPUID_VMX,
        "VMX reason");
    Sample = ValidSample(VALID_VMX_BASIC); Sample.Cr4 = CR4_CET;
    TEST_ASSERT(!VmxPlatformReadinessSampleValidate(&Sample, &Reason),
        "CET rejected");
    TEST_ASSERT_UINT_EQ(Reason, VMX_PLATFORM_READINESS_REASON_CET_ACTIVE,
        "CET reason");
    Sample = ValidSample(VALID_VMX_BASIC); Sample.FeatureControl = 0;
    TEST_ASSERT(!VmxPlatformReadinessSampleValidate(&Sample, &Reason),
        "unsafe feature control rejected");
    TEST_ASSERT_UINT_EQ(Reason, VMX_PLATFORM_READINESS_REASON_FEATURE_CONTROL,
        "feature-control reason");
    Sample = ValidSample(0);
    TEST_ASSERT(!VmxPlatformReadinessSampleValidate(&Sample, &Reason),
        "invalid VMX basic rejected");
    TEST_ASSERT_UINT_EQ(Reason, VMX_PLATFORM_READINESS_REASON_VMX_BASIC,
        "VMX-basic reason");
    return 0;
}

static int TestUniformConsensus(void)
{
    VMX_PLATFORM_READINESS_CONSENSUS Consensus = { 0 };
    VMX_PLATFORM_READINESS_SAMPLE Sample = ValidSample(VALID_VMX_BASIC);
    uint32_t Index;

    TEST_ASSERT(VmxPlatformReadinessConsensusBegin(4, &Consensus),
        "consensus begin");
    for (Index = 0; Index < 4; ++Index) {
        TEST_ASSERT(VmxPlatformReadinessConsensusObserve(
            &Consensus, Index, &Sample), "uniform observation");
    }
    TEST_ASSERT(VmxPlatformReadinessConsensusComplete(&Consensus),
        "uniform consensus complete");
    TEST_ASSERT_UINT_EQ(Consensus.Status,
        VMX_PLATFORM_READINESS_CONSENSUS_ADMITTED, "admitted status");
    TEST_ASSERT_UINT_EQ(Consensus.ObservedProcessors, 4,
        "observed processors");
    TEST_ASSERT_UINT_EQ(Consensus.VmxBasic, VALID_VMX_BASIC,
        "consensus VMX basic");
    TEST_ASSERT_UINT_EQ(Consensus.PreemptionTimerRate, 5,
        "consensus timer rate");
    TEST_ASSERT_UINT_EQ(Consensus.PreemptionTimerAvailable, 1,
        "consensus timer available");
    return 0;
}

static int TestUniformTimerAbsenceAdmitted(void)
{
    VMX_PLATFORM_READINESS_CONSENSUS Consensus = { 0 };
    VMX_PLATFORM_READINESS_SAMPLE Sample = ValidSample(VALID_VMX_BASIC);
    uint32_t Index;

    Sample.PinControls = 0;
    TEST_ASSERT(VmxPlatformReadinessConsensusBegin(4, &Consensus),
        "timer-absent begin");
    for (Index = 0; Index < 4; ++Index) {
        TEST_ASSERT(VmxPlatformReadinessConsensusObserve(
            &Consensus, Index, &Sample), "timer-absent observation");
    }
    TEST_ASSERT(VmxPlatformReadinessConsensusComplete(&Consensus),
        "timer-absent consensus complete");
    TEST_ASSERT_UINT_EQ(Consensus.Status,
        VMX_PLATFORM_READINESS_CONSENSUS_ADMITTED,
        "timer-absent admitted status");
    TEST_ASSERT_UINT_EQ(Consensus.PreemptionTimerAvailable, 0,
        "timer-absent availability");
    TEST_ASSERT_UINT_EQ(Consensus.PreemptionTimerRate, 0,
        "timer-absent rate zero");
    return 0;
}

static int TestMismatchRejected(void)
{
    VMX_PLATFORM_READINESS_CONSENSUS Consensus = { 0 };
    VMX_PLATFORM_READINESS_SAMPLE First = ValidSample(VALID_VMX_BASIC);
    VMX_PLATFORM_READINESS_SAMPLE Second =
        ValidSample(VALID_VMX_BASIC | UINT64_C(4));

    TEST_ASSERT(VmxPlatformReadinessConsensusBegin(2, &Consensus),
        "mismatch begin");
    TEST_ASSERT(VmxPlatformReadinessConsensusObserve(&Consensus, 0, &First),
        "mismatch first");
    TEST_ASSERT(!VmxPlatformReadinessConsensusObserve(&Consensus, 1, &Second),
        "mismatch rejected");
    TEST_ASSERT_UINT_EQ(Consensus.RejectionReason,
        VMX_PLATFORM_READINESS_REASON_VMX_BASIC_MISMATCH,
        "mismatch reason");
    TEST_ASSERT_UINT_EQ(Consensus.RejectedSystemIndex, 1,
        "mismatch index");
    TEST_ASSERT(!VmxPlatformReadinessConsensusComplete(&Consensus),
        "denied consensus cannot complete");
    return 0;
}

static int TestConsensusFraming(void)
{
    VMX_PLATFORM_READINESS_CONSENSUS Consensus = { 0 };
    VMX_PLATFORM_READINESS_SAMPLE Sample = ValidSample(VALID_VMX_BASIC);

    TEST_ASSERT(!VmxPlatformReadinessConsensusBegin(0, &Consensus),
        "zero count rejected");
    TEST_ASSERT(VmxPlatformReadinessConsensusBegin(2, &Consensus),
        "framing begin");
    TEST_ASSERT(!VmxPlatformReadinessConsensusObserve(&Consensus, 1, &Sample),
        "skipped index rejected");
    TEST_ASSERT(VmxPlatformReadinessConsensusObserve(&Consensus, 0, &Sample),
        "first observation");
    TEST_ASSERT(!VmxPlatformReadinessConsensusComplete(&Consensus),
        "incomplete rejected");
    TEST_ASSERT(!VmxPlatformReadinessConsensusObserve(&Consensus, 0, &Sample),
        "duplicate rejected");
    TEST_ASSERT(VmxPlatformReadinessConsensusObserve(&Consensus, 1, &Sample),
        "second observation");
    TEST_ASSERT(!VmxPlatformReadinessConsensusObserve(&Consensus, 2, &Sample),
        "extra observation rejected");
    TEST_ASSERT(VmxPlatformReadinessConsensusComplete(&Consensus),
        "framing complete");
    TEST_ASSERT(!VmxPlatformReadinessConsensusObserve(&Consensus, 2, &Sample),
        "finalized observation rejected");
    return 0;
}

static int TestTimerRateMismatchRejected(void)
{
    VMX_PLATFORM_READINESS_CONSENSUS Consensus = { 0 };
    VMX_PLATFORM_READINESS_SAMPLE First = ValidSample(VALID_VMX_BASIC);
    VMX_PLATFORM_READINESS_SAMPLE Second = ValidSample(VALID_VMX_BASIC);
    Second.VmxMisc = UINT64_C(6);

    TEST_ASSERT(VmxPlatformReadinessConsensusBegin(2, &Consensus),
        "timer mismatch begin");
    TEST_ASSERT(VmxPlatformReadinessConsensusObserve(&Consensus, 0, &First),
        "timer mismatch first");
    TEST_ASSERT(!VmxPlatformReadinessConsensusObserve(&Consensus, 1, &Second),
        "timer mismatch rejected");
    TEST_ASSERT_UINT_EQ(Consensus.RejectionReason,
        VMX_PLATFORM_READINESS_REASON_TIMER_RATE_MISMATCH,
        "timer mismatch reason");
    return 0;
}

static int TestTimerCapabilityMismatchRejected(void)
{
    VMX_PLATFORM_READINESS_CONSENSUS Consensus = { 0 };
    VMX_PLATFORM_READINESS_SAMPLE First = ValidSample(VALID_VMX_BASIC);
    VMX_PLATFORM_READINESS_SAMPLE Second = ValidSample(VALID_VMX_BASIC);

    Second.PinControls = 0;
    TEST_ASSERT(VmxPlatformReadinessConsensusBegin(2, &Consensus),
        "timer capability mismatch begin");
    TEST_ASSERT(VmxPlatformReadinessConsensusObserve(&Consensus, 0, &First),
        "timer capability mismatch first");
    TEST_ASSERT(!VmxPlatformReadinessConsensusObserve(&Consensus, 1, &Second),
        "timer capability mismatch rejected");
    TEST_ASSERT_UINT_EQ(Consensus.RejectionReason,
        VMX_PLATFORM_READINESS_REASON_TIMER_CAPABILITY_MISMATCH,
        "timer capability mismatch reason");
    TEST_ASSERT_UINT_EQ(Consensus.RejectedSystemIndex, 1,
        "timer capability mismatch index");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestValidSample);
    TEST_RUN(TestInvalidSamples);
    TEST_RUN(TestUniformConsensus);
    TEST_RUN(TestUniformTimerAbsenceAdmitted);
    TEST_RUN(TestMismatchRejected);
    TEST_RUN(TestConsensusFraming);
    TEST_RUN(TestTimerRateMismatchRejected);
    TEST_RUN(TestTimerCapabilityMismatchRejected);
TEST_MAIN_END()
