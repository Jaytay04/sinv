#include <string.h>
#include "test_common.h"
#include "vmx_cr3_route_owner.h"

#define CPU_COUNT 3u

static VMX_CR3_TARGET_SET Set(void)
{
    const uint64_t Roots[2] = { UINT64_C(0x12345000), UINT64_C(0x23456000) };
    VMX_CR3_TARGET_SET Set = { 0 };
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, 1, Roots, 2, 9, &Set),
        VMX_CR3_TARGET_OK, "target set");
    return Set;
}

static uint64_t Qualification(uint32_t Gpr)
{
    return VMX_CR3_EXIT_CR3 | ((uint64_t)Gpr << VMX_CR3_EXIT_GPR_SHIFT);
}

static void PlanCommitReset(void)
{
    VMX_CR3_TARGET_SET Target = Set();
    VMX_CR3_RESIDENCY_STATE States[CPU_COUNT] = { 0 };
    VMX_CR3_ROUTE_OWNER Owner = { 0 };
    VMX_CR3_ROUTE_PLAN Plan = { 0 };
    VMX_CR3_TARGET_SET SnapshotTarget = { 0 };
    VMX_CR3_RESIDENCY_STATE SnapshotStates[CPU_COUNT] = { 0 };
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    Gprs[7] = Target.Roots[1] | UINT64_C(5) | VMX_CR3_TARGET_NO_FLUSH;
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerInitialize(&Target, States, CPU_COUNT,
        &Owner), VMX_CR3_TARGET_OK, "owner initialize");
    TEST_ASSERT_TRUE(Owner.States == States && Owner.Count == CPU_COUNT,
        "owner contracts");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerValidate(&Owner), VMX_CR3_TARGET_OK,
        "owner validate");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerPlan(&Owner, 1, Qualification(7), Gprs,
        VMX_CR3_EXIT_GPR_COUNT, UINT64_C(0xffff800000001000), 3, &Plan),
        VMX_CR3_TARGET_OK, "processor one plan");
    TEST_ASSERT_TRUE(States[0].TransitionSequence == 0 &&
        States[1].TransitionSequence == 0, "plan does not commit");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerCommit(&Owner, 1, &Plan),
        VMX_CR3_TARGET_OK, "commit exact plan");
    TEST_ASSERT_TRUE(!States[0].Resident && States[1].Resident &&
        States[1].MatchedIndex == 1 && States[1].Pcid == 5 &&
        States[1].TransitionSequence == 1, "per processor committed state");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerSnapshot(&Owner, &SnapshotTarget,
        SnapshotStates, CPU_COUNT), VMX_CR3_TARGET_OK, "coherent snapshot" );
    TEST_ASSERT_TRUE(SnapshotTarget.Generation == Target.Generation &&
        SnapshotStates[0].TransitionSequence == 0 &&
        SnapshotStates[1].TransitionSequence == 1 && SnapshotStates[1].Resident,
        "snapshot exact per processor state");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerCommit(&Owner, 1, &Plan),
        VMX_CR3_TARGET_INVARIANT, "stale plan rejected");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerReset(&Owner), VMX_CR3_TARGET_OK,
        "owner reset");
    TEST_ASSERT_TRUE(memcmp(&Owner, &(VMX_CR3_ROUTE_OWNER){0}, sizeof(Owner)) == 0 &&
        memcmp(States, (VMX_CR3_RESIDENCY_STATE[CPU_COUNT]){{0}}, sizeof(States)) == 0,
        "reset zeroes owner and states");
}

static void RejectsAndPreserves(void)
{
    VMX_CR3_TARGET_SET Target = Set();
    VMX_CR3_RESIDENCY_STATE States[CPU_COUNT] = { 0 };
    VMX_CR3_ROUTE_OWNER Owner = { 0 }, Before;
    VMX_CR3_ROUTE_PLAN Plan = { 0 }, Mutated;
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerInitialize(&Target, States, CPU_COUNT,
        &Owner), VMX_CR3_TARGET_OK, "reject fixture");
    Before = Owner;
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerPlan(&Owner, CPU_COUNT, Qualification(0),
        Gprs, VMX_CR3_EXIT_GPR_COUNT, 0x1000, 3, &Plan),
        VMX_CR3_TARGET_INVALID, "index bound");
    TEST_ASSERT_TRUE(memcmp(&Owner, &Before, sizeof(Owner)) == 0,
        "bad index preserves owner");
    Gprs[0] = Target.Roots[0];
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerPlan(&Owner, 0, Qualification(0), Gprs,
        VMX_CR3_EXIT_GPR_COUNT, 0x1000, 3, &Plan), VMX_CR3_TARGET_OK,
        "mutation fixture");
#define BAD_PLAN(change, message) do { \
    Mutated = Plan; change; Before = Owner; \
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerCommit(&Owner, 0, &Mutated), \
        VMX_CR3_TARGET_INVARIANT, message); \
    TEST_ASSERT_TRUE(memcmp(&Owner, &Before, sizeof(Owner)) == 0, \
        message " preserves owner"); \
} while (0)
    BAD_PLAN(Mutated.Transition.Next.TargetGeneration++, "generation mutation");
    BAD_PLAN(Mutated.Transition.Next.NormalizedRoot += 0x1000,
        "root mutation");
    BAD_PLAN(Mutated.Transition.Sequence++, "sequence mutation");
#undef BAD_PLAN
    {
        VMX_CR3_TARGET_SET SnapshotTarget = { 0 };
        VMX_CR3_RESIDENCY_STATE SnapshotStates[CPU_COUNT] = { 0 };
        States[1].Busy = 1;
        TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerSnapshot(&Owner, &SnapshotTarget,
            SnapshotStates, CPU_COUNT), VMX_CR3_TARGET_BUSY,
            "busy snapshot rejected");
        TEST_ASSERT_TRUE(memcmp(&SnapshotTarget,
            &(VMX_CR3_TARGET_SET){0}, sizeof(SnapshotTarget)) == 0 &&
            memcmp(SnapshotStates,
                (VMX_CR3_RESIDENCY_STATE[CPU_COUNT]){{0}},
                sizeof(SnapshotStates)) == 0, "busy snapshot clears outputs");
        States[1].Busy = 0;
    }
    Before = Owner; Owner.TargetGenerationContract++;
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerValidate(&Owner), VMX_CR3_TARGET_INVALID,
        "owner validate rejects mutation");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerPlan(&Owner, 0, Qualification(0), Gprs,
        VMX_CR3_EXIT_GPR_COUNT, 0x1000, 3, &(VMX_CR3_ROUTE_PLAN){0}),
        VMX_CR3_TARGET_INVALID, "owner contract mutation");
    Owner = Before;
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerReset(&Owner), VMX_CR3_TARGET_OK,
        "reject fixture reset");
}

static void GuardDisablesTargetClassification(void)
{
    VMX_CR3_TARGET_SET Target = Set();
    VMX_CR3_RESIDENCY_STATE States[CPU_COUNT] = { 0 };
    VMX_CR3_ROUTE_OWNER Owner = { 0 };
    VMX_CR3_ROUTE_PLAN Plan = { 0 };
    volatile uint32_t TargetState = 2;
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };

    Gprs[0] = Target.Roots[0];
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerInitializeGuarded(&Target,
        &TargetState, 2, States, CPU_COUNT, &Owner), VMX_CR3_TARGET_OK,
        "guarded owner initialize");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerPlan(&Owner, 0, Qualification(0), Gprs,
        VMX_CR3_EXIT_GPR_COUNT, UINT64_C(0x1000), 3, &Plan),
        VMX_CR3_TARGET_OK, "active guard plan");
    TEST_ASSERT_UINT_EQ(Plan.Transition.Action,
        VMX_CR3_RESIDENCY_ACTION_ENTER, "active guard enters");

    memset(&Plan, 0, sizeof(Plan));
    TargetState = 3;
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerPlan(&Owner, 0, Qualification(0), Gprs,
        VMX_CR3_EXIT_GPR_COUNT, UINT64_C(0x1000), 3, &Plan),
        VMX_CR3_TARGET_OK, "expired guard plan");
    TEST_ASSERT_UINT_EQ(Plan.Transition.Action,
        VMX_CR3_RESIDENCY_ACTION_REMAIN_OUTSIDE,
        "expired guard remains outside");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerCommit(&Owner, 0, &Plan),
        VMX_CR3_TARGET_OK, "expired nonresident commit");
    TEST_ASSERT_UINT_EQ(States[0].Resident, 0,
        "expired guard cannot publish residency");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerReset(&Owner), VMX_CR3_TARGET_OK,
        "guarded owner reset");
}

static void GuardExpiryBetweenPlanAndCommitCannotPublishResidency(void)
{
    VMX_CR3_TARGET_SET Target = Set();
    VMX_CR3_RESIDENCY_STATE States[CPU_COUNT] = { 0 };
    VMX_CR3_ROUTE_OWNER Owner = { 0 };
    VMX_CR3_ROUTE_PLAN Plan = { 0 };
    volatile uint32_t TargetState = 2;
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };

    Gprs[0] = Target.Roots[0];
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerInitializeGuarded(&Target,
        &TargetState, 2, States, CPU_COUNT, &Owner), VMX_CR3_TARGET_OK,
        "expiry race owner initialize");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerPlan(&Owner, 0, Qualification(0), Gprs,
        VMX_CR3_EXIT_GPR_COUNT, UINT64_C(0x1000), 3, &Plan),
        VMX_CR3_TARGET_OK, "active pre-expiry plan");
    TEST_ASSERT_UINT_EQ(Plan.Transition.Action,
        VMX_CR3_RESIDENCY_ACTION_ENTER, "pre-expiry plan enters");

    TargetState = 3;
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerCommit(&Owner, 0, &Plan),
        VMX_CR3_TARGET_OK, "stale active plan commits fail-closed");
    TEST_ASSERT_UINT_EQ(States[0].Resident, 0,
        "expiry race cannot publish residency");
    TEST_ASSERT_UINT_EQ(States[0].MatchedIndex, UINT32_MAX,
        "expiry race clears matched index");
    TEST_ASSERT_INT_EQ(VmxCr3RouteOwnerReset(&Owner), VMX_CR3_TARGET_OK,
        "expiry race owner reset");
}

int main(void)
{
    PlanCommitReset();
    RejectsAndPreserves();
    GuardDisablesTargetClassification();
    GuardExpiryBetweenPlanAndCommitCannotPublishResidency();
    return 0;
}
