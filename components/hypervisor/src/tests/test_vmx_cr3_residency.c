#include <string.h>
#include "test_common.h"
#include "vmx_cr3_residency.h"

static VMX_CR3_TARGET_SET Set(void)
{
    const uint64_t Roots[2] = { UINT64_C(0x12345000), UINT64_C(0x6789a000) };
    VMX_CR3_TARGET_SET Value = { 0 };
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, 1, Roots, 2, 7, &Value),
        VMX_CR3_TARGET_OK, "set");
    return Value;
}

static VMX_CR3_EXIT_DECISION Decode(const VMX_CR3_TARGET_SET* Targets,
    uint64_t Operand)
{
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    VMX_CR3_EXIT_DECISION Value = { 0 };
    Gprs[5] = Operand;
    TEST_ASSERT_INT_EQ(VmxCr3ExitDecode(Targets,
        VMX_CR3_EXIT_CR3 | (UINT64_C(5) << VMX_CR3_EXIT_GPR_SHIFT),
        Gprs, VMX_CR3_EXIT_GPR_COUNT, &Value), VMX_CR3_TARGET_OK, "decode");
    return Value;
}

static void Lifecycle(void)
{
    VMX_CR3_TARGET_SET Targets = Set();
    VMX_CR3_RESIDENCY_STATE State = { 0 };
    VMX_CR3_RESIDENCY_TRANSITION Step = { 0 };
    VMX_CR3_EXIT_DECISION Decision;
    TEST_ASSERT_INT_EQ(VmxCr3ResidencyInitialize(7, &State),
        VMX_CR3_TARGET_OK, "initialize");
    TEST_ASSERT_UINT_EQ(State.MatchedIndex, UINT32_MAX, "outside index");

#define APPLY(operand, action, resident, sequence, message) do { \
    Decision = Decode(&Targets, (operand)); memset(&Step, 0, sizeof(Step)); \
    TEST_ASSERT_INT_EQ(VmxCr3ResidencyApply(&State, &Decision, &Step), \
        VMX_CR3_TARGET_OK, (message)); \
    TEST_ASSERT_UINT_EQ(Step.Action, (action), message " action"); \
    TEST_ASSERT_UINT_EQ(Step.CurrentResident, (resident), message " resident"); \
    TEST_ASSERT_UINT_EQ(Step.Sequence, (sequence), message " sequence"); \
    State = Step.Next; \
} while (0)
    APPLY(UINT64_C(0xabcde000), VMX_CR3_RESIDENCY_ACTION_REMAIN_OUTSIDE,
        0, 1, "remain outside");
    APPLY(Targets.Roots[0], VMX_CR3_RESIDENCY_ACTION_ENTER, 1, 2,
        "enter target");
    APPLY(Targets.Roots[1] | 7 | VMX_CR3_TARGET_NO_FLUSH,
        VMX_CR3_RESIDENCY_ACTION_REMAIN_INSIDE, 1, 3, "KVA-root stay");
    TEST_ASSERT_UINT_EQ(State.MatchedIndex, 1, "KVA-root index update");
    TEST_ASSERT_UINT_EQ(State.Pcid, 7, "PCID update");
    APPLY(UINT64_C(0xabcde000), VMX_CR3_RESIDENCY_ACTION_LEAVE, 0, 4,
        "leave target");
#undef APPLY
}

static void RejectsAndPreserves(void)
{
    VMX_CR3_TARGET_SET Targets = Set();
    VMX_CR3_RESIDENCY_STATE State = { 0 };
    VMX_CR3_EXIT_DECISION Decision = Decode(&Targets, Targets.Roots[0]);
    VMX_CR3_RESIDENCY_TRANSITION Out, Before;
    TEST_ASSERT_INT_EQ(VmxCr3ResidencyInitialize(7, &State),
        VMX_CR3_TARGET_OK, "reject fixture");

#define REJECT(change, status, message) do { \
    change; memset(&Out, 0, sizeof(Out)); Before = Out; \
    TEST_ASSERT_INT_EQ(VmxCr3ResidencyApply(&State, &Decision, &Out), \
        (status), (message)); \
    TEST_ASSERT_TRUE(memcmp(&Out, &Before, sizeof(Out)) == 0, \
        message " preserves"); \
} while (0)
    REJECT(Decision.Target.Generation = 8, VMX_CR3_TARGET_INVARIANT,
        "stale generation");
    Decision = Decode(&Targets, Targets.Roots[0]);
    REJECT(Decision.Action = VMX_CR3_EXIT_ACTION_INVALID,
        VMX_CR3_TARGET_INVALID, "invalid decision");
    Decision = Decode(&Targets, Targets.Roots[0]);
    REJECT(Decision.Target.NormalizedRoot = UINT64_C(1) << 60,
        VMX_CR3_TARGET_INVALID, "oversized root");
    Decision = Decode(&Targets, Targets.Roots[0]);
    REJECT(State.TransitionSequence = UINT64_MAX, VMX_CR3_TARGET_INVARIANT,
        "sequence overflow" );
    State.TransitionSequence = 0;
    REJECT(State.Busy = 1, VMX_CR3_TARGET_INVALID,
        "busy state");
#undef REJECT

    memset(&Out, 0, sizeof(Out));
    TEST_ASSERT_INT_EQ(VmxCr3ResidencyApply(
        (const VMX_CR3_RESIDENCY_STATE*)&Out, &Decision, &Out),
        VMX_CR3_TARGET_INVALID, "state/output alias");
    memset(&Out, 0, sizeof(Out));
    TEST_ASSERT_INT_EQ(VmxCr3ResidencyApply(&State,
        (const VMX_CR3_EXIT_DECISION*)&Out, &Out),
        VMX_CR3_TARGET_INVALID, "decision/output alias");
}

int main(void)
{
    Lifecycle();
    RejectsAndPreserves();
    return 0;
}
