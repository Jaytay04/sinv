#include <string.h>
#include "test_common.h"
#include "vmx_cr3_route_plan.h"

static void Fixture(VMX_CR3_TARGET_SET* Set,
    VMX_CR3_RESIDENCY_STATE* State)
{
    const uint64_t Roots[2] = { UINT64_C(0x12345000), UINT64_C(0x6789a000) };
    memset(Set, 0, sizeof(*Set)); memset(State, 0, sizeof(*State));
    TEST_ASSERT_INT_EQ(VmxCr3TargetSetBuild(48, 1, Roots, 2, 9, Set),
        VMX_CR3_TARGET_OK, "set");
    TEST_ASSERT_INT_EQ(VmxCr3ResidencyInitialize(9, State),
        VMX_CR3_TARGET_OK, "state");
}

static VMX_CR3_ROUTE_PLAN Build(VMX_CR3_TARGET_SET* Set,
    VMX_CR3_RESIDENCY_STATE* State, uint64_t Operand)
{
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    VMX_CR3_ROUTE_PLAN Plan = { 0 };
    Gprs[11] = Operand;
    TEST_ASSERT_INT_EQ(VmxCr3RoutePlanBuild(Set, State,
        VMX_CR3_EXIT_CR3 | (UINT64_C(11) << VMX_CR3_EXIT_GPR_SHIFT),
        Gprs, VMX_CR3_EXIT_GPR_COUNT, UINT64_C(0xffff800000001000), 3,
        &Plan), VMX_CR3_TARGET_OK, "route build");
    return Plan;
}

static void Transitions(void)
{
    VMX_CR3_TARGET_SET Set;
    VMX_CR3_RESIDENCY_STATE State;
    VMX_CR3_ROUTE_PLAN Plan;
    Fixture(&Set, &State);
    Plan = Build(&Set, &State, Set.Roots[0] | 5);
    TEST_ASSERT_UINT_EQ(Plan.Action,
        VMX_CR3_ROUTE_ACTION_WRITE_CR3_ADVANCE_RESUME, "route action");
    TEST_ASSERT_UINT_EQ(Plan.GuestCr3, Set.Roots[0] | 5, "exact CR3");
    TEST_ASSERT_UINT_EQ(Plan.AdvancedRip, UINT64_C(0xffff800000001003),
        "advanced RIP");
    TEST_ASSERT_UINT_EQ(Plan.Transition.Action,
        VMX_CR3_RESIDENCY_ACTION_ENTER, "enter");
    State = Plan.Transition.Next;
    Plan = Build(&Set, &State,
        Set.Roots[1] | 7 | VMX_CR3_TARGET_NO_FLUSH);
    TEST_ASSERT_UINT_EQ(Plan.Transition.Action,
        VMX_CR3_RESIDENCY_ACTION_REMAIN_INSIDE, "KVA stay");
    State = Plan.Transition.Next;
    Plan = Build(&Set, &State, UINT64_C(0xabcde000));
    TEST_ASSERT_UINT_EQ(Plan.Transition.Action,
        VMX_CR3_RESIDENCY_ACTION_LEAVE, "leave");
}

static void RejectsAndPreserves(void)
{
    VMX_CR3_TARGET_SET Set;
    VMX_CR3_RESIDENCY_STATE State;
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    VMX_CR3_ROUTE_PLAN Out, Before;
    Fixture(&Set, &State); Gprs[0] = Set.Roots[0];
#define BAD(rip, length, count, message) do { \
    memset(&Out, 0, sizeof(Out)); Before = Out; \
    TEST_ASSERT_INT_EQ(VmxCr3RoutePlanBuild(&Set, &State, \
        VMX_CR3_EXIT_CR3, Gprs, (count), (rip), (length), &Out), \
        VMX_CR3_TARGET_INVALID, (message)); \
    TEST_ASSERT_TRUE(memcmp(&Out, &Before, sizeof(Out)) == 0, \
        message " preserves"); \
} while (0)
    BAD(UINT64_C(0x0000800000000000), 3, VMX_CR3_EXIT_GPR_COUNT,
        "noncanonical RIP");
    BAD(UINT64_MAX, 3, VMX_CR3_EXIT_GPR_COUNT, "RIP overflow");
    BAD(UINT64_C(0x00007ffffffffffe), 3, VMX_CR3_EXIT_GPR_COUNT,
        "advanced noncanonical");
    BAD(UINT64_C(0x1000), 0, VMX_CR3_EXIT_GPR_COUNT, "zero length");
    BAD(UINT64_C(0x1000), 16, VMX_CR3_EXIT_GPR_COUNT, "long length");
    BAD(UINT64_C(0x1000), 3, VMX_CR3_EXIT_GPR_COUNT - 1, "short GPRs");
#undef BAD
    State.TargetGeneration++;
    memset(&Out, 0, sizeof(Out)); Before = Out;
    TEST_ASSERT_INT_EQ(VmxCr3RoutePlanBuild(&Set, &State, VMX_CR3_EXIT_CR3,
        Gprs, VMX_CR3_EXIT_GPR_COUNT, UINT64_C(0x1000), 3, &Out),
        VMX_CR3_TARGET_INVARIANT, "stale generation");
    TEST_ASSERT_TRUE(memcmp(&Out, &Before, sizeof(Out)) == 0,
        "stale preserves");
    memset(&Out, 0, sizeof(Out));
    TEST_ASSERT_INT_EQ(VmxCr3RoutePlanBuild(&Set,
        (const VMX_CR3_RESIDENCY_STATE*)&Out, VMX_CR3_EXIT_CR3, Gprs,
        VMX_CR3_EXIT_GPR_COUNT, UINT64_C(0x1000), 3, &Out),
        VMX_CR3_TARGET_INVALID, "state/output alias");
}

static void DisabledTargetStillEmulates(void)
{
    VMX_CR3_TARGET_SET Set;
    VMX_CR3_RESIDENCY_STATE State;
    VMX_CR3_ROUTE_PLAN Plan = { 0 };
    uint64_t Gprs[VMX_CR3_EXIT_GPR_COUNT] = { 0 };
    Fixture(&Set, &State);
    Gprs[11] = Set.Roots[0] | 5;
    TEST_ASSERT_INT_EQ(VmxCr3RoutePlanBuildTargetState(&Set, &State, 0,
        VMX_CR3_EXIT_CR3 | (UINT64_C(11) << VMX_CR3_EXIT_GPR_SHIFT),
        Gprs, VMX_CR3_EXIT_GPR_COUNT, UINT64_C(0xffff800000001000), 3,
        &Plan), VMX_CR3_TARGET_OK, "disabled route build");
    TEST_ASSERT_UINT_EQ(Plan.GuestCr3, Set.Roots[0] | 5,
        "disabled exact CR3");
    TEST_ASSERT_UINT_EQ(Plan.AdvancedRip, UINT64_C(0xffff800000001003),
        "disabled advanced RIP");
    TEST_ASSERT_UINT_EQ(Plan.Transition.Action,
        VMX_CR3_RESIDENCY_ACTION_REMAIN_OUTSIDE,
        "disabled target remains outside");
    TEST_ASSERT_UINT_EQ(Plan.Transition.CurrentResident, 0,
        "disabled target is nonresident");
}

int main(void)
{
    Transitions(); RejectsAndPreserves(); DisabledTargetStillEmulates();
    return 0;
}
